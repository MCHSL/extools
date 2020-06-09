#include "../core/core.h"
#include "../dmdism/instruction.h"
#include "../dmdism/disassembly.h"
#include <set>
#include "../third_party/asmjit/asmjit.h"
#include <fstream>
#include "jit.h"
#include <algorithm>

using namespace asmjit;

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

void get_arg(x86::Compiler& cc, unsigned int id)
{
	get_value(cc, x86::ptr(arglist_ptr), id);
}

void push_integer(x86::Compiler& cc, float f)
{
	stack.push_back(Imm(DataType::NUMBER));
	union
	{
		float f;
		int i;
	} heck;
	heck.f = f;
	stack.push_back(Imm(heck.i));
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
	cc.mov(x86::edx, stack.at(stack.size() - 1).as<Imm>());
	cc.mov(x86::eax, stack.at(stack.size() - 2).as<Imm>());
	cc.ret();
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
	cc.mov(x86::edx, Imm(0));
	cc.mov(x86::eax, Imm(0));
	cc.ret();
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
	stack.erase(stack.end() - 4);
	x86::Xmm fleft = cc.newXmm("left_op");
	x86::Xmm fright = cc.newXmm("right_op");
	if (left_v.isImm())
	{
		x86::Mem l = cc.newInt32Const(ConstPool::kScopeLocal, left_v.as<Imm>().value());
		cc.movd(fleft, l);
	}
	else
	{
		cc.movd(fleft, left_v.as<x86::Gp>());
	}

	if (right_v.isImm())
	{
		x86::Mem r = cc.newInt32Const(ConstPool::kScopeLocal, right_v.as<Imm>().value());
		cc.movd(fright, r);
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

trvh call_global_wrapper(unsigned int proc_id, trvh* args, unsigned int num_args)
{
	return CallGlobalProc(0, 0, 2, proc_id, 0, DataType::NULL_D, 0, (Value*)args, num_args, 0, 0);
	//return Core::get_proc(proc_id).call(std::vector<Value>(args, args + num_args));
}

void call_global(x86::Compiler& cc, unsigned int arg_count, unsigned int proc_id)
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
	x86::Gp addrholder = cc.newInt32();
	cc.lea(addrholder, args);
	stack.erase(stack.end() - arg_count, stack.end());
	x86::Gp ret_type = cc.newInt32();
	x86::Gp ret_value = cc.newInt32();
	auto call = cc.call((uint64_t)CallGlobalProc, FuncSignatureT<int, int, int, int, int, int, int, int, int*, int, int, int>());
	call->setArg(0, Imm(0));
	call->setArg(1, Imm(0));
	call->setArg(2, Imm(2));
	call->setArg(3, Imm(proc_id));
	call->setArg(4, Imm(0));
	call->setArg(5, Imm(0));
	call->setArg(6, Imm(0));
	call->setArg(7, addrholder);
	call->setArg(8, Imm(arg_count));
	call->setArg(9, Imm(0));
	call->setArg(10, Imm(0));
	call->setRet(0, ret_type);
	cc.mov(ret_value, x86::edx);
	stack.push_back(ret_type);
	stack.push_back(ret_value);
}

void compile_block(x86::Compiler& cc, Block& block, std::map<unsigned int, Block> blocks)
{
	cc.bind(block.label);
	for (int i=0; i<block.contents.size(); i++)
	{
		Instruction& instr = block.contents[i];
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
		default:
			jit_out << "Unknown instruction: " << instr.opcode().tostring() << std::endl;
			break;
		}
	}
}

JitRuntime rt;
void jit_compile(Core::Proc& p)
{
	FILE* fuck = fopen("asm.txt", "w");
	FileLogger logger(fuck);
	SimpleErrorHandler eh;
	CodeHolder code;
	code.init(rt.codeInfo());
	code.setLogger(&logger);
	code.setErrorHandler(&eh);
	x86::Compiler cc(&code);
	cc.addFunc(FuncSignatureT<int, int, int*, int, int>(CallConv::kIdHostCDecl));

	Disassembly dis = p.disassemble();
	std::ofstream asd("raw.txt");
	for (Instruction& i : dis)
	{
		asd << i.bytes_str() << std::endl;
	}
	auto blocks = split_into_blocks(dis, cc);
	set_arg_ptr(cc);
	for (auto& [k, v] : blocks)
	{
		compile_block(cc, v, blocks);
	}
	cc.endFunc();

	jit_out << "Finalizing\n";
	int err = cc.finalize();
	if (err)
	{
		jit_out << "Failed to assemble" << std::endl;
		return;
	}

	ProcHook hook;
	err = rt.add(&hook, &code);
	if (err)
	{
		jit_out << "Failed to add to runtime: " << err << std::endl;
		return;
	}
	jit_out << "Compilation successful" << std::endl;
	jit_out << hook << std::endl;
	p.hook(hook);
}