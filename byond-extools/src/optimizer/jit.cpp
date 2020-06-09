#include "../core/core.h"
#include "../dmdism/instruction.h"
#include "../dmdism/disassembly.h"
#include <set>
#include "../third_party/asmjit/asmjit.h"
#include <fstream>
#include "jit.h"
#include <algorithm>

using namespace asmjit;

// Shouldn't be globals!!!
static std::map<unsigned int, FuncNode*> proc_funcs;
static std::set<std::string> string_set;

static uint32_t EncodeFloat(float f)
{
	return *reinterpret_cast<uint32_t*>(&f);
}

static float DecodeFloat(uint32_t i)
{
	return *reinterpret_cast<float*>(&i);
}

std::map<unsigned int, Block> split_into_blocks(Disassembly& dis, x86::Compiler& cc)
{
	std::map<unsigned int, Block> blocks;
	unsigned int current_block_offset = 0;
	blocks[current_block_offset] = Block(0);
	std::set<unsigned int> jump_targets;
	for (Instruction& i : dis)
	{
		if (jump_targets.find(i.offset()) != jump_targets.end())
		{
			current_block_offset = i.offset();
			blocks[current_block_offset] = Block(current_block_offset);
			jump_targets.erase(i.offset());
		}
		blocks[current_block_offset].contents.push_back(i);
		if (i == Bytecode::JZ || i == Bytecode::JMP || i == Bytecode::JMP2)
		{
			const unsigned int target = i.jump_locations().at(0);
			if (target > i.offset())
			{
				jump_targets.insert(i.jump_locations().at(0));
			}
			else //we need to split a block in twain
			{
				Block& victim = (--blocks.lower_bound(target))->second; //get the block that contains the target offset
				auto split_point = std::find_if(victim.contents.begin(), victim.contents.end(), [target](Instruction& instr) { return instr.offset() == target; }); //find the target instruction
				Block new_block = Block(target);
				new_block.contents = std::vector<Instruction>(split_point, victim.contents.end());
				victim.contents.erase(split_point, victim.contents.end()); //split
				blocks[target] = new_block;
			}
			current_block_offset = i.offset()+i.size();
			blocks[current_block_offset] = Block(current_block_offset);
		}
	}
	std::ofstream o("blocks.txt");
	for (auto& [offset, block] : blocks)
	{
		block.label = cc.newLabel();
		o << offset << ":\n";
		for (const Instruction& i : block.contents)
		{
			o << i.opcode().tostring() << "\n";
		}
		o << "\n";
	}
	return blocks;
}

std::ofstream jit_out("jit_out.txt");

class SimpleErrorHandler : public asmjit::ErrorHandler
{
public:
	void handleError(asmjit::Error err, const char* message, asmjit::BaseEmitter* origin) override
	{
		this->err = err;
		jit_out << message << "\n";
	}

	Error err;
};

std::map<unsigned int, x86::Mem> locals;
std::vector<Operand> stack;

void set_value(x86::Compiler& cc, x86::Mem& loc, Operand& type, Operand& value, unsigned int offset = 0)
{
	loc.setSize(4);
	loc.setOffset(offset * sizeof(trvh) + offsetof(trvh, type));
	cc.mov(loc, type.as<Imm>());
	loc.setOffset(offset * sizeof(trvh) + offsetof(trvh, value));
	cc.mov(loc, value.as<Imm>());
}

void get_value(x86::Compiler& cc, x86::Mem& loc, unsigned int offset = 0)
{
	x86::Gp tp = cc.newInt32("type");
	x86::Gp val = cc.newInt32("value");
	loc.setOffset(offset * sizeof(trvh) + offsetof(trvh, type));
	cc.mov(tp, loc);
	stack.push_back(tp);
	loc .setOffset(offset * sizeof(trvh) + offsetof(trvh, value));
	cc.mov(val, loc);
	stack.push_back(val);
}

void set_local(x86::Compiler& cc, unsigned int id)
{
	if (locals.find(id) == locals.end())
	{
		locals[id] = cc.newStack(sizeof(trvh), 4);
	}
	set_value(cc, locals[id], stack.at(stack.size() - 2), stack.at(stack.size() - 1));
	stack.erase(stack.end() - 2, stack.end());
}

void get_local(x86::Compiler& cc, unsigned int id)
{
	if (locals.find(id) == locals.end())
	{
		locals[id] = cc.newStack(sizeof(trvh), 4);
		set_value(cc, locals[id], Imm(0), Imm(0));
	}
	get_value(cc, locals[id]);
}

x86::Gp arglist_ptr;
void set_arg_ptr(x86::Compiler& cc)
{
	arglist_ptr = cc.newInt32("arglist_ptr");
	cc.setArg(1, arglist_ptr);
}

x86::Gp src_type;
x86::Gp src_value;
void set_src(x86::Compiler& cc)
{
	src_type = cc.newInt32("src_type");
	src_value = cc.newInt32("src_value");
	cc.setArg(2, src_type);
	cc.setArg(3, src_value);
}

void get_src(x86::Compiler& cc)
{
	stack.push_back(src_type);
	stack.push_back(src_value);
}

void get_arg(x86::Compiler& cc, unsigned int id)
{
	get_value(cc, x86::ptr(arglist_ptr), id);
}

void push_integer(x86::Compiler& cc, float f)
{
	stack.push_back(Imm(DataType::NUMBER));
	stack.push_back(Imm(EncodeFloat(f)));
}

bool compare_values(trvh left, trvh right)
{
	return left.value == right.value && left.type == right.type;
}

void get_variable(x86::Compiler& cc, const Instruction& instr)
{
	auto& base = instr.acc_base;
	switch (base.first)
	{
	case AccessModifier::ARG:
		get_arg(cc, base.second);
		break;
	case AccessModifier::LOCAL:
		get_local(cc, base.second);
		break;
	case AccessModifier::SRC:
		get_src(cc);
		break;
	default:
		Core::Alert("Unknown access modifier in jit get_variable");
		break;
	}
	x86::Gp type = stack.at(stack.size() - 2).as<x86::Gp>();
	x86::Gp value = stack.at(stack.size() - 1).as<x86::Gp>();
	stack.erase(stack.end() - 2, stack.end());

	for (unsigned int name : instr.acc_chain)
	{
		auto call = cc.call((uint64_t)GetVariable, FuncSignatureT<int, int, int, int>());
		call->setArg(0, type);
		call->setArg(1, value);
		call->setArg(2, Imm(name));
		call->setRet(0, type);
		cc.mov(value, x86::edx);
	}
	
	stack.push_back(type);
	stack.push_back(value);

}

void test_equal(x86::Compiler& cc)
{
	auto call = cc.call((uint64_t)compare_values, FuncSignatureT<bool, int, int, int, int>());
	call->setArg(0, stack.at(stack.size() - 4).as<Imm>());
	call->setArg(1, stack.at(stack.size() - 3).as<Imm>());
	call->setArg(2, stack.at(stack.size() - 2).as<Imm>());
	call->setArg(3, stack.at(stack.size() - 1).as<Imm>());
	stack.erase(stack.end() - 4, stack.end());
	x86::Gp result = cc.newInt8("cmp_result");
	call->setRet(0, result);
	stack.push_back(result);
}

void ret(x86::Compiler& cc)
{
	x86::Gp ret_type = cc.newInt32();
	x86::Gp ret_value = cc.newInt32();

	cc.mov(ret_type, stack.at(stack.size() - 2).as<Imm>());
	cc.mov(ret_value, stack.at(stack.size() - 1).as<Imm>());
	cc.ret(ret_type, ret_value);
	stack.erase(stack.end() - 2, stack.end());
}

void jump_zero(x86::Compiler& cc, Block& destination)
{
	x86::Gp cond = stack.at(stack.size() - 1).as<x86::Gp>();
	stack.erase(stack.end() - 1, stack.end());
	cc.test(cond, cond);
	cc.je(destination.label);
}

void end(x86::Compiler& cc)
{
	x86::Gp ret_type = cc.newInt32();
	x86::Gp ret_value = cc.newInt32();
	cc.mov(ret_type, Imm(0));
	cc.mov(ret_value, Imm(0));
	cc.ret(ret_type, ret_value);
}

void jump(x86::Compiler& cc, Block& dest)
{
	cc.jmp(dest.label);
}

void math_op(x86::Compiler& cc, Bytecode op)
{
	Operand left_t = stack.at(stack.size() - 4);
	Operand left_v = stack.at(stack.size() - 3);
	Operand right_t = stack.at(stack.size() - 2);
	Operand right_v = stack.at(stack.size() - 1);
	stack.erase(stack.end() - 4, stack.end());
	x86::Xmm fleft = cc.newXmm("left_op");
	x86::Xmm fright = cc.newXmm("right_op");
	if (left_v.isImm())
	{
		x86::Mem l = cc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(left_v.as<Imm>().value()));
		cc.movss(fleft, l);
	}
	else
	{
		cc.movd(fleft, left_v.as<x86::Gp>());
	}

	if (right_v.isImm())
	{
		x86::Mem r = cc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(right_v.as<Imm>().value()));
		cc.movss(fright, r);
	}
	else
	{
		cc.movd(fright, right_v.as<x86::Gp>());
	}
	switch (op)
	{
	case Bytecode::ADD:
		cc.addss(fleft, fright);
		break;
	case Bytecode::SUB:
		cc.subss(fleft, fright);
		break;
	case Bytecode::MUL:
		cc.mulss(fleft, fright);
		break;
	case Bytecode::DIV:
		cc.divss(fleft, fright);
		break;
	}
	x86::Gp res_type = cc.newInt32("res_type");
	x86::Gp res_value = cc.newInt32("res_value");
	cc.mov(res_type, Imm(0x2A));
	cc.movd(res_value, fleft);
	stack.push_back(res_type);
	stack.push_back(res_value);
}

void call_compiled_global(x86::Compiler& cc, unsigned int arg_count, FuncNode* func)
{
	x86::Mem args = cc.newStack(sizeof(trvh) * arg_count, 4);
	x86::Mem args_i = args.clone();
	args.setSize(4);
	args_i.setSize(4);
	for (int i = 0; i < arg_count; i++)
	{
		args_i.setOffset(sizeof(trvh) * i + offsetof(trvh, type));
		Operand type = stack.at(stack.size() - arg_count * 2 + i * 2);
		cc.mov(args_i, type.as<Imm>());
		args_i.setOffset(sizeof(trvh) * i + offsetof(trvh, value));
		Operand value = stack.at(stack.size() - arg_count * 2 + i * 2 + 1);
		cc.mov(args_i, value.as<Imm>());
	}

	x86::Gp argsPtr = cc.newIntPtr();
	cc.lea(argsPtr, args);
	stack.erase(stack.end() - arg_count * 2, stack.end());

	x86::Gp ret_type = cc.newInt32();
	x86::Gp ret_value = cc.newInt32();

	InvokeNode* invocation;
	cc.invoke(&invocation, func->label(), FuncSignatureT<asmjit::Type::I64, int, int*, int>(CallConv::kIdCDecl));
	invocation->setArg(0, Imm(arg_count));
	invocation->setArg(1, argsPtr);			// We only we use this one atm
	invocation->setArg(2, Imm(0));
	invocation->setRet(0, ret_type);
	invocation->setRet(1, ret_value);

	stack.push_back(ret_type);
	stack.push_back(ret_value);
}

void call_global(x86::Compiler& cc, unsigned int arg_count, unsigned int proc_id)
{
	auto proc_func = proc_funcs.find(proc_id);
	if(proc_func != proc_funcs.end())
	{
		call_compiled_global(cc, arg_count, proc_func->second);
		return;
	}

	x86::Mem args = cc.newStack(sizeof(trvh) * arg_count, 4);
	x86::Mem args_i = args.clone();
	args.setSize(4);
	args_i.setSize(4);
	for (int i = 0; i < arg_count; i++)
	{
		args_i.setOffset(sizeof(trvh) * i + offsetof(trvh, type));
		Operand type = stack.at(stack.size() - arg_count * 2 + i * 2);
		cc.mov(args_i, type.as<Imm>());
		args_i.setOffset(sizeof(trvh) * i + offsetof(trvh, value));
		Operand value = stack.at(stack.size() - arg_count * 2 + i * 2 + 1);
		cc.mov(args_i, value.as<Imm>());
	}
	x86::Gp arg_ptr = cc.newInt32();
	cc.lea(arg_ptr, args);
	stack.erase(stack.end() - arg_count * 2, stack.end());
	x86::Gp ret_type = cc.newInt32();
	x86::Gp ret_value = cc.newInt32();
	auto call = cc.call((uint64_t)CallGlobalProc, FuncSignatureT<asmjit::Type::I64, int, int, int, int, int, int, int, int*, int, int, int>());
	call->setArg(0, Imm(0));
	call->setArg(1, Imm(0));
	call->setArg(2, Imm(2));
	call->setArg(3, Imm(proc_id));
	call->setArg(4, Imm(0));
	call->setArg(5, Imm(0));
	call->setArg(6, Imm(0));
	call->setArg(7, arg_ptr);
	call->setArg(8, Imm(arg_count));
	call->setArg(9, Imm(0));
	call->setArg(10, Imm(0));
	call->setRet(0, ret_type);
	call->setRet(1, ret_value);

	stack.push_back(ret_type);
	stack.push_back(ret_value);
}

void call_proc(x86::Compiler& cc, unsigned int proc_id, unsigned int arg_count)
{
	x86::Gp src_t = stack.at(stack.size() - 2).as<x86::Gp>();
	x86::Gp src_v = stack.at(stack.size() - 1).as<x86::Gp>();
	stack.erase(stack.end() - 2, stack.end());
	x86::Mem args = cc.newStack(sizeof(trvh) * arg_count, 4); //TODO: EXTRACT LOADING ARGUMENTS INTO A FUNCTION!
	x86::Mem args_i = args.clone();
	args.setSize(4);
	args_i.setSize(4);
	for (int i = 0; i < arg_count; i++)
	{
		args_i.setOffset(sizeof(trvh) * i + offsetof(trvh, type));
		Operand type = stack.at(stack.size() - arg_count * 2 + i * 2);
		cc.mov(args_i, type.as<Imm>());
		args_i.setOffset(sizeof(trvh) * i + offsetof(trvh, value));
		Operand value = stack.at(stack.size() - arg_count * 2 + i * 2 + 1);
		cc.mov(args_i, value.as<Imm>());
	}
	x86::Gp arg_ptr = cc.newInt32();
	cc.lea(arg_ptr, args);
	stack.erase(stack.end() - arg_count * 2, stack.end());
	x86::Gp ret_type = cc.newInt32();
	x86::Gp ret_value = cc.newInt32();

	auto call = cc.call((uint64_t)CallProcByName, FuncSignatureT<asmjit::Type::I64, int, int, int, int, int, int, int*, int, int, int>());
	call->setArg(0, Imm(0));
	call->setArg(1, Imm(0));
	call->setArg(2, Imm(2));
	call->setArg(3, Imm(proc_id));
	call->setArg(4, src_t);
	call->setArg(5, src_v);
	call->setArg(6, arg_ptr);
	call->setArg(7, Imm(arg_count));
	call->setArg(8, Imm(0));
	call->setArg(9, Imm(0));
	call->setRet(0, ret_type);
	call->setRet(1, ret_value);

	stack.push_back(ret_type);
	stack.push_back(ret_value);
}

void call(x86::Compiler& cc, Instruction& instr)
{
	if (instr.acc_base.first == AccessModifier::SRC)
	{
		get_src(cc);
		call_proc(cc, instr.bytes()[4], instr.bytes()[5]);
	}
	else
	{
		switch (instr.acc_base.first)
		{
		case AccessModifier::ARG:
			get_arg(cc, instr.acc_base.second);
			break;
		case AccessModifier::LOCAL:
			get_local(cc, instr.acc_base.second);
			break;
		default:
			Core::Alert("Invalid access modifier base");
			break;
		}
		call_proc(cc, instr.bytes()[5], instr.bytes()[6]);
	}
}

void push_value(x86::Compiler& cc, DataType type, unsigned int value, unsigned int value2 = 0)
{
	if (type == DataType::NUMBER)
	{
		value = value << 16 | value2;
	}
	stack.push_back(Imm(type));
	stack.push_back(Imm(value));
}

void compile_block(x86::Compiler& cc, Block& block, std::map<unsigned int, Block> blocks)
{
	cc.bind(block.label);
	for (int i=0; i<block.contents.size(); i++)
	{
		Instruction& instr = block.contents[i];

		auto it = string_set.insert(instr.opcode().tostring());
		cc.setInlineComment(it.first->c_str());

		switch (instr.bytes()[0])
		{
		case Bytecode::PUSHI:
			jit_out << "Assembling push integer" << std::endl;
			push_integer(cc, instr.bytes()[1]);
			break;
		case Bytecode::ADD:
		case Bytecode::SUB:
		case Bytecode::MUL:
		case Bytecode::DIV:
			jit_out << "Assembling math operation" << std::endl;
			math_op(cc, instr.opcode().opcode());
			break;
		case Bytecode::SETVAR:
			if (instr.bytes()[1] == AccessModifier::LOCAL)
			{
				jit_out << "Assembling set local" << std::endl;
				set_local(cc, instr.bytes()[2]);
			}
			break;
		case Bytecode::GETVAR:
			if (instr.bytes()[1] == AccessModifier::ARG)
			{
				jit_out << "Assembling get arg" << std::endl;
				get_arg(cc, instr.bytes()[2]);
			}
			else if (instr.bytes()[1] == AccessModifier::LOCAL)
			{
				jit_out << "Assembling get local" << std::endl;
				get_local(cc, instr.bytes()[2]);
			}
			else if (instr.bytes()[1] == AccessModifier::SUBVAR)
			{
				jit_out << "Assembling get subvar" << std::endl;
				get_variable(cc, instr);	
			}
			else if (instr.bytes()[1] == AccessModifier::SRC)
			{
				jit_out << "Assembling get src" << std::endl;
				get_src(cc);
			}
			break;
		case Bytecode::TEQ:
			jit_out << "Assembling test equal" << std::endl;
			test_equal(cc);
			i += 1; //skip	POP
			break;
		case Bytecode::JZ:
			jit_out << "Assembling jump zero" << std::endl;
			jump_zero(cc, blocks[instr.bytes()[1]]);
			break;
		case Bytecode::JMP:
		case Bytecode::JMP2:
			jit_out << "Assembling unconditional jump" << std::endl;
			jump(cc, blocks[instr.bytes()[1]]);
			break;
		case Bytecode::RET:
			jit_out << "Assembling return" << std::endl;
			ret(cc);
			break;
		case Bytecode::END:
			jit_out << "Assembling end" << std::endl;
			end(cc);
			break;
		case Bytecode::CALLGLOB:
			jit_out << "Assembling call global" << std::endl;
			call_global(cc, instr.bytes()[1], instr.bytes()[2]);
			break;
		case Bytecode::CALL:
			jit_out << "Assembling normal call" << std::endl;
			call(cc, instr);
			break;
		case Bytecode::PUSHVAL:
			jit_out << "Assembling push value" << std::endl;
			if (instr.bytes()[1] == DataType::NUMBER) //numbers take up two DWORDs instead of one
			{
				push_value(cc, (DataType)instr.bytes()[1], instr.bytes()[2], instr.bytes()[3]);
			}
			else
			{
				push_value(cc, (DataType)instr.bytes()[1], instr.bytes()[2]);
			}
			break;
		case Bytecode::DBG_FILE:
		case Bytecode::DBG_LINENO:
			break;
		default:
			jit_out << "Unknown instruction: " << instr.opcode().tostring() << std::endl;
			break;
		}
	}
}

JitRuntime rt;
void jit_compile(std::vector<Core::Proc*> procs)
{
	FILE* fuck = fopen("asm.txt", "w");
	FileLogger logger(fuck);
	SimpleErrorHandler eh;
	CodeHolder code;
	code.init(rt.codeInfo());
	code.setLogger(&logger);
	code.setErrorHandler(&eh);
	x86::Compiler cc(&code);
	std::ofstream asd("raw.txt");

	for (auto& proc : procs)
	{
		FuncNode* func = cc.newFunc(FuncSignatureT<asmjit::Type::I64, int, int*, int, int>(CallConv::kIdCDecl));
		std::string comment = "BEGIN PROC: " + proc->name;
		auto it = string_set.insert(comment);
		func->setInlineComment(it.first->c_str());

		proc_funcs[proc->id] = func;
	}

	// Emit funcs
	for (auto& proc : procs)
	{
		cc.addFunc(proc_funcs[proc->id]);

		Disassembly dis = proc->disassemble();
		
		asd << "BEGIN " << proc->name << '\n';
		for (Instruction& i : dis)
		{
			asd << i.bytes_str() << std::endl;
		}
		asd << "END " << proc->name << '\n';

		auto blocks = split_into_blocks(dis, cc);
		set_arg_ptr(cc);
		set_src(cc);
		for (auto& [k, v] : blocks)
		{
			compile_block(cc, v, blocks);
		}
		cc.endFunc();
	}

	jit_out << "Finalizing\n";
	int err = cc.finalize();
	if (err)
	{
		jit_out << "Failed to assemble" << std::endl;
		return;
	}

	char* code_base = nullptr;
	err = rt.add(&code_base, &code);
	if (err)
	{
		jit_out << "Failed to add to runtime: " << err << std::endl;
		return;
	}

	// Hook procs
	for (auto& proc : procs)
	{
		FuncNode* func = proc_funcs[proc->id];
		ProcHook func_base = reinterpret_cast<ProcHook>(code_base + code.labelOffset(func->label()));
		jit_out << func_base << std::endl;
		proc->hook(func_base);
	}

	jit_out << "Compilation successful" << std::endl;	
}

extern "C" EXPORT const char* jit_initialize(int n_args, const char** args)
{
	if (!Core::initialize())
	{
		return Core::FAIL;
	}
	jit_compile({&Core::get_proc("/proc/addthesetwo"), &Core::get_proc("/proc/jit"), &Core::get_proc("/obj/proc/jit")});
	return Core::SUCCESS;
}
