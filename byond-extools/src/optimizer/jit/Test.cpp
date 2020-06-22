#include "Test.h"
#include "DMCompiler.h"
#include "JitContext.h"

#include "../../core/core.h"
#include "../../dmdism/instruction.h"
#include "../../dmdism/disassembly.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <array>

using namespace asmjit;
using namespace dmjit;

static std::ofstream jit_out("jit_out.txt");
static std::ofstream byteout_out("bytecode.txt");
static std::ofstream blocks_out("blocks.txt");
static asmjit::JitRuntime rt;

class SimpleErrorHandler : public asmjit::ErrorHandler
{
public:
	void handleError(asmjit::Error err, const char* message, asmjit::BaseEmitter* origin) override
	{
		this->err = err;
		jit_out << message << "\n";
	}

	asmjit::Error err;
};

struct ProcBlock
{
	ProcBlock(unsigned int o) : offset(o) {}
	ProcBlock() : offset(0) {}
	std::vector<Instruction> contents;
	unsigned int offset;
	asmjit::Label label;
	BlockNode* node;
};

static std::map<unsigned int, ProcBlock> split_into_blocks(Disassembly& dis, DMCompiler& dmc, size_t& locals_max_count, size_t& args_max_count)
{
	std::map<unsigned int, ProcBlock> blocks;
	unsigned int current_block_offset = 0;
	blocks[current_block_offset] = ProcBlock(0);
	std::set<unsigned int> jump_targets;
	for (Instruction& i : dis)
	{
		if(i == Bytecode::SETVAR || i == Bytecode::GETVAR)
		{
			switch(i.bytes()[1])
			{
			case AccessModifier::LOCAL:
				locals_max_count = std::max(locals_max_count, i.bytes()[2] + 1);
				break;
			case AccessModifier::ARG:
				args_max_count = std::max(args_max_count, i.bytes()[2] + 1);
				break;
			default:
				break;
			}
		}

		if (jump_targets.find(i.offset()) != jump_targets.end())
		{
			current_block_offset = i.offset();
			blocks[current_block_offset] = ProcBlock(current_block_offset);
			jump_targets.erase(i.offset());
		}
		blocks[current_block_offset].contents.push_back(i);
		if (i == Bytecode::JZ || i == Bytecode::JMP || i == Bytecode::JMP2 || i == Bytecode::JNZ || i == Bytecode::JNZ2)
		{
			const unsigned int target = i.jump_locations().at(0);
			if (blocks.find(target) == blocks.end())
			{
				if (target > i.offset())
				{
					jump_targets.insert(i.jump_locations().at(0));
				}
				else //we need to split a block in twain
				{
					ProcBlock& victim = (--blocks.lower_bound(target))->second; //get the block that contains the target offset
					auto split_point = std::find_if(victim.contents.begin(), victim.contents.end(), [target](Instruction& instr) { return instr.offset() == target; }); //find the target instruction
					ProcBlock new_block = ProcBlock(target);
					new_block.contents = std::vector<Instruction>(split_point, victim.contents.end());
					victim.contents.erase(split_point, victim.contents.end()); //split
					blocks[target] = new_block;
				}
			}
			current_block_offset = i.offset()+i.size();
			blocks[current_block_offset] = ProcBlock(current_block_offset);
		}
	}
	
	blocks_out << "BEGIN: " << dis.proc->name << '\n';
	for (auto& [offset, block] : blocks)
	{
		block.label = dmc.newLabel();
		blocks_out << std::hex << offset << ":\n";
		for (const Instruction& i : block.contents)
		{
			blocks_out << std::hex << i.offset() << std::dec << "\t\t\t" << i.bytes_str() << "\t\t\t" << i.opcode().mnemonic() << "\n";
		}
		blocks_out << "\n";
	}
	blocks_out << '\n';

	return blocks;
}

static uint32_t EncodeFloat(float f)
{
	return *reinterpret_cast<uint32_t*>(&f);
}

static float DecodeFloat(uint32_t i)
{
	return *reinterpret_cast<float*>(&i);
}

static void Emit_PushInteger(DMCompiler& dmc, float not_an_integer)
{
	dmc.pushStack(Variable{Imm(DataType::NUMBER), Imm(EncodeFloat(not_an_integer))});
}

static void Emit_SetLocal(DMCompiler& dmc, int index)
{
	dmc.setLocal(index, dmc.popStack());
}

static void Emit_GetLocal(DMCompiler& dmc, int index)
{
	dmc.pushStack(dmc.getLocal(index));
}

static void Emit_Pop(DMCompiler& dmc)
{
	dmc.popStack();
}

static void Emit_PushValue(DMCompiler& dmc, DataType type, unsigned int value, unsigned int value2 = 0)
{
	dmc.pushStack(Variable{Imm(type), Imm(value)});
}

static void Emit_Return(DMCompiler& dmc)
{
	dmc.doReturn();
}

static unsigned int add_strings(unsigned int str1, unsigned int str2)
{
	return Core::GetStringId(Core::GetStringFromId(str1) + Core::GetStringFromId(str2));
}

static void Emit_MathOp(DMCompiler& dmc, Bytecode op_type)
{
	auto [lhs, rhs] = dmc.popStack<2>();
	Variable result = dmc.pushStack2(Imm(DataType::NUMBER), Imm(0));

	auto done_adding_strings = dmc.newLabel();
	if (op_type == Bytecode::ADD)
	{
		auto reg = dmc.newUInt32();
		dmc.mov(reg, lhs.Type.as<x86::Gp>());
		dmc.cmp(reg, Imm(DataType::STRING));
		auto notstring = dmc.newLabel();
		dmc.jne(notstring);

		auto call = dmc.call((uint32_t)add_strings, FuncSignatureT<unsigned int, unsigned int, unsigned int>());
		call->setArg(0, lhs.Value.as<x86::Gp>());
		call->setArg(1, rhs.Value.as<x86::Gp>());
		call->setRet(0, result.Value.as<x86::Gp>());
		dmc.mov(result.Type.as<x86::Gp>(), Imm(DataType::STRING));
		dmc.jmp(done_adding_strings);
		dmc.bind(notstring);
	}


	auto xmm0 = dmc.newXmm();
	auto xmm1 = dmc.newXmm();

	if (lhs.Value.isImm())
	{
		auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(lhs.Value.as<Imm>().value()));
		dmc.movd(xmm0, data);
	}
	else
	{
		dmc.movd(xmm0, lhs.Value.as<x86::Gp>());
	}

	if (rhs.Value.isImm())
	{
		auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(rhs.Value.as<Imm>().value()));
		dmc.movd(xmm1, data);
	}
	else
	{
		dmc.movd(xmm1, rhs.Value.as<x86::Gp>());
	}

	switch (op_type)
	{
		case Bytecode::ADD:
			dmc.addss(xmm0, xmm1);
			break;
		case Bytecode::SUB:
			dmc.subss(xmm0, xmm1);
			break;
		case Bytecode::MUL:
			dmc.mulss(xmm0, xmm1);
			break;
		case Bytecode::DIV:
			dmc.divss(xmm0, xmm1);
			break;
	}

	dmc.movd(result.Value.as<x86::Gp>(), xmm0);
	dmc.bind(done_adding_strings);
}

static void Emit_CallGlobal(DMCompiler& dmc, uint32_t arg_count, uint32_t proc_id)
{
	//std::vector<Variable> args;

	x86::Mem args = dmc.newStack(sizeof(Value) * arg_count, 4);
	args.setSize(sizeof(uint32_t));
	uint32_t arg_i = arg_count;
	while (arg_i--)
	{
		Variable var = dmc.popStack();

		args.setOffset(arg_i * sizeof(Value) + offsetof(Value, type));
		if (var.Type.isImm())
		{
			auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(var.Type.as<Imm>().value()));
			dmc.mov(args, var.Type.as<Imm>());
		}
		else
		{
			dmc.mov(args, var.Type.as<x86::Gp>());
		}

		args.setOffset(arg_i * sizeof(Value) + offsetof(Value, value));
		if (var.Value.isImm())
		{
			auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(var.Value.as<Imm>().value()));
			dmc.mov(args, var.Value.as<Imm>());
		}
		else
		{
			dmc.mov(args, var.Value.as<x86::Gp>());
		}		
	}
	args.setOffset(0);

	// dmc.commitLocals();
	// dmc.commitStack();

	x86::Gp args_ptr = dmc.newUIntPtr();
	dmc.lea(args_ptr, args);

	x86::Gp ret_type = dmc.newUIntPtr();
	x86::Gp ret_value = dmc.newUIntPtr();

	auto call = dmc.call((uint64_t)CallGlobalProc, FuncSignatureT<asmjit::Type::I64, int, int, int, int, int, int, int, int*, int, int, int>());
	call->setArg(0, Imm(0));
	call->setArg(1, Imm(0));
	call->setArg(2, Imm(2));
	call->setArg(3, Imm(proc_id));
	call->setArg(4, Imm(0));
	call->setArg(5, Imm(0));
	call->setArg(6, Imm(0));
	call->setArg(7, args_ptr);
	call->setArg(8, Imm(arg_count));
	call->setArg(9, Imm(0));
	call->setArg(10, Imm(0));
	call->setRet(0, ret_type);
	call->setRet(1, ret_value);

	dmc.pushStack(Variable{ret_type, ret_value});
}

static DMListIterator* create_iterator(ProcStackFrame* psf, int list_id)
{
	DMListIterator* iter = new DMListIterator;
	RawList* list = GetListPointerById(list_id);
	iter->elements = new Value[list->length];
	std::copy(list->vector_part, list->vector_part + list->length, iter->elements);
	iter->length = list->length;
	iter->current_index = -1; // -1 because the index is imemdiately incremented by ITERNEXT
	iter->previous = nullptr;
	if (psf->current_iterator)
	{
		DMListIterator* current = psf->current_iterator;
		iter->previous = current;
	}
	psf->current_iterator = iter;
	return iter;

}

static DMListIterator* pop_iterator(ProcStackFrame* psf)
{
	DMListIterator* current = psf->current_iterator;
	psf->current_iterator = current->previous;
	delete[] current->elements;
	delete current;
	return psf->current_iterator;
}

static void Emit_Iterload(DMCompiler& dmc)
{
	auto list = dmc.popStack();
	auto new_iter = dmc.newUIntPtr();
	auto create_iter = dmc.call((uint64_t)create_iterator, FuncSignatureT<int*, int*, int>());
	create_iter->setArg(0, dmc.getStackFrame());
	create_iter->setArg(1, list.Value.as<x86::Gp>());
	create_iter->setRet(0, new_iter);
	dmc.setCurrentIterator(new_iter);
}

static void Emit_Iterpop(DMCompiler& dmc)
{
	auto new_iter = dmc.newUIntPtr();
	auto create_iter = dmc.call((uint64_t)pop_iterator, FuncSignatureT<int*, int*>());
	create_iter->setArg(0, dmc.getStackFrame());
	create_iter->setRet(0, new_iter);
	dmc.setCurrentIterator(new_iter);
}

// How much to shift when using x86::ptr - [base + (offset << shift)]
unsigned int shift(unsigned int n)
{
	return log2(n);
}

// DM bytecode pushes the next value to the stack, immediately sets a local variable to it then checks to zero flag.
// Doing it the same way by compiling the next few instructions would screw up our zero flag, so we do all of it here.
static void Emit_Iternext(DMCompiler& dmc, Instruction& setvar, Instruction& jmp, std::map<unsigned int, ProcBlock>& blocks)
{
	auto current_iter = dmc.getCurrentIterator();
	auto reg = dmc.newUInt32();
	dmc.mov(reg, x86::ptr(current_iter, offsetof(DMListIterator, current_index)));
	dmc.inc(reg);
	dmc.commitStack();
	dmc.cmp(reg, x86::ptr(current_iter, offsetof(DMListIterator, length)));
	dmc.jge(blocks.at(jmp.bytes().at(1)).label);
	dmc.mov(x86::ptr(current_iter, offsetof(DMListIterator, current_index)), reg);
	auto elements = dmc.newUIntPtr();
	dmc.mov(elements, x86::ptr(current_iter, offsetof(DMListIterator, elements)));
	auto type = dmc.newUInt32();
	dmc.mov(type, x86::ptr(elements, reg, shift(sizeof(Value))));
	auto value = dmc.newUInt32();
	dmc.mov(value, x86::ptr(elements, reg, shift(sizeof(Value)), offsetof(Value, value)));
	dmc.setLocal(setvar.bytes().at(2), Variable{type, value});
}

static void Emit_Jump(DMCompiler& dmc, uint32_t target, std::map<unsigned int, ProcBlock>& blocks)
{
	dmc.jump(blocks.at(target).label);
}

static void Emit_GetListElement(DMCompiler& dmc)
{
	auto [container, key] = dmc.popStack<2>();
	auto getelem = dmc.call((uint32_t)GetAssocElement, FuncSignatureT<asmjit::Type::I64, int, int, int, int>());
	getelem->setArg(0, container.Type.as<x86::Gp>());
	getelem->setArg(1, container.Value.as<x86::Gp>());
	getelem->setArg(2, key.Type.as<x86::Gp>());
	getelem->setArg(3, key.Value.as<x86::Gp>());
	auto type = dmc.newUInt32();
	auto value = dmc.newUInt32();
	getelem->setRet(0, type);
	getelem->setRet(1, value);
	dmc.pushStack(Variable{ type, value });
}

static void Emit_SetListElement(DMCompiler& dmc)
{
	auto [container, key, value] = dmc.popStack<3>();
	auto getelem = dmc.call((uint32_t)SetAssocElement, FuncSignatureT<void, int, int, int, int, int, int>());
	getelem->setArg(0, container.Type.as<x86::Gp>());
	getelem->setArg(1, container.Value.as<x86::Gp>());
	getelem->setArg(2, key.Type.as<x86::Gp>());
	getelem->setArg(3, key.Value.as<x86::Gp>());
	getelem->setArg(4, value.Type.as<x86::Gp>());
	getelem->setArg(5, value.Value.as<x86::Gp>());
}

static void Emit_Output(DMCompiler& dmc)
{

}

static void Emit_End(DMCompiler& dmc)
{
	dmc.pushStack(dmc.getDot());
	dmc.doReturn();
}

static bool Emit_Block(DMCompiler& dmc, ProcBlock& block, std::map<unsigned int, ProcBlock>& blocks)
{
	block.node = dmc.addBlock(block.label);

	for (size_t i=0; i < block.contents.size(); i++)
	{
		Instruction& instr = block.contents[i];

		switch (instr.bytes()[0])
		{
		case Bytecode::PUSHI:
			jit_out << "Assembling push integer" << std::endl;
			dmc.setInlineComment("push integer");
			Emit_PushInteger(dmc, instr.bytes()[1]);
			break;
		case Bytecode::ADD:
		case Bytecode::SUB:
		case Bytecode::MUL:
		case Bytecode::DIV:
			jit_out << "Assembling math op" << std::endl;
			dmc.setInlineComment("math operation");
			Emit_MathOp(dmc, (Bytecode)instr.bytes()[0]);
			break;
		case Bytecode::SETVAR:
			switch (instr.bytes()[1])
			{
			case AccessModifier::LOCAL:
				jit_out << "Assembling set local" << std::endl;
				dmc.setInlineComment("set local");
				dmc.setLocal(instr.bytes()[2], dmc.popStack());
				break;
			case AccessModifier::DOT:
				jit_out << "Assembling set dot" << std::endl;
				dmc.setInlineComment("set dot");
				dmc.setDot(dmc.popStack());
				break;
			default:
				Core::Alert("Failed to assemble setvar");
				break;
			}
			break;
		case Bytecode::GETVAR:
			switch (instr.bytes()[1])
			{
			case AccessModifier::LOCAL:
				dmc.pushStack(dmc.getLocal(instr.bytes()[2]));
				break;
			case AccessModifier::ARG:
				dmc.pushStack(dmc.getArg(instr.bytes()[2]));
				break;
			case AccessModifier::SRC:
				dmc.pushStack(dmc.getSrc());
				break;
			case AccessModifier::DOT:
				dmc.pushStack(dmc.getDot());
				break;
			default:
				Core::Alert("Failed to assemble getvar");
				break;
			}
			break;
		case Bytecode::POP:
			jit_out << "Assembling pop" << std::endl;
			dmc.setInlineComment("pop from stack");
			Emit_Pop(dmc);
			break;
		case Bytecode::PUSHVAL:
			jit_out << "Assembling push value" << std::endl;
			dmc.setInlineComment("push value");
			if (instr.bytes()[1] == DataType::NUMBER) //numbers take up two DWORDs instead of one
			{
				Emit_PushValue(dmc, (DataType)instr.bytes()[1], instr.bytes()[2] << 16 | instr.bytes()[3]);
			}
			else
			{
				Emit_PushValue(dmc, (DataType)instr.bytes()[1], instr.bytes()[2]);
			}
			break;
		case Bytecode::CALLGLOB:
			jit_out << "Assembling call global" << std::endl;
			dmc.setInlineComment("call global proc");
			Emit_CallGlobal(dmc, instr.bytes()[1], instr.bytes()[2]);
			break;
		case Bytecode::LISTGET:
			jit_out << "Assembling list get" << std::endl;
			dmc.setInlineComment("list get");
			Emit_GetListElement(dmc);
			break;
		case Bytecode::LISTSET:
			jit_out << "Assembling list set" << std::endl;
			dmc.setInlineComment("list set");
			Emit_SetListElement(dmc);
			break;
		case Bytecode::ITERLOAD:
			jit_out << "Assembling iterator load" << std::endl;
			dmc.setInlineComment("load iterator");
			Emit_Iterload(dmc);
			break;
		case Bytecode::ITERNEXT:
			jit_out << "Assembling iterator next" << std::endl;
			dmc.setInlineComment("iterator next");
			Emit_Iternext(dmc, block.contents[i + 1], block.contents[i + 2], blocks);
			i += 2;
			break;
		case Bytecode::ITERATOR_PUSH:
			break; //What this opcode does is already handled in ITERLOAD
		case Bytecode::ITERATOR_POP:
			jit_out << "Assembling iterator pop" << std::endl;
			Emit_Iterpop(dmc);
			break;
		case Bytecode::JMP:
		case Bytecode::JMP2:
			jit_out << "Assembling jump" << std::endl;
			Emit_Jump(dmc, instr.bytes()[1], blocks);
			break;
		case Bytecode::RET:
			Emit_Return(dmc);
			break;
		case Bytecode::END:
			jit_out << "Assembling end" << std::endl;
			dmc.setInlineComment("end");
			Emit_End(dmc);
			break;
		case Bytecode::DBG_FILE:
		case Bytecode::DBG_LINENO:
			break;
		default:
			jit_out << "Unknown instruction: " << instr.opcode().tostring() << std::endl;
			break;
		}
	}

	dmc.endBlock();
	return true;
}

static trvh JitEntryPoint(void* code_base, unsigned int args_len, Value* args, Value src, Value usr)
{
	JitContext ctx;
	Proc code = static_cast<Proc>(code_base);

	ProcResult res = code(&ctx, 0, args_len, args, src, usr);

	switch (res)
	{
	case ProcResult::Success:
		if (ctx.Count() != 1)
		{
			__debugbreak();
			return Value::Null();
		}

		return ctx.stack[0];
	case ProcResult::Yielded:
		return Value::Null();
	}

	// Shouldn't be here
	__debugbreak();
	return Value::Null();
}

void* honk;

static void compile(std::vector<Core::Proc*> procs)
{
	FILE* fuck = fopen("asm.txt", "w");
	asmjit::FileLogger logger(fuck);
	SimpleErrorHandler eh;
	asmjit::CodeHolder code;
	code.init(rt.codeInfo());
	code.setLogger(&logger);
	code.setErrorHandler(&eh);

	DMCompiler dmc(code);

	for (auto& proc : procs)
	{
		Disassembly dis = proc->disassemble();
		byteout_out << "BEGIN " << proc->name << '\n';
		for (Instruction i : dis)
		{
			byteout_out << i.bytes_str() << std::endl;
		}
		byteout_out << "END " << proc->name << '\n';

		size_t locals_count = 1; // Defaulting to 1 because asmjit does not like empty vectors apparently (causes an assertion error)
		size_t args_count = 1;
		auto blocks = split_into_blocks(dis, dmc, locals_count, args_count);

		dmc.addProc(locals_count, args_count);
		for (auto& [k, v] : blocks)
		{
			Emit_Block(dmc, v, blocks);
		}
		dmc.endProc();
	}

	dmc.finalize();

	rt.add(&honk, &code);
	jit_out << honk << std::endl;
	
}

trvh invoke_hook(unsigned int n_args, Value* args, Value src)
{
	return JitEntryPoint(honk, n_args, args, src, Value::Null());
}

EXPORT const char* ::jit_test(int n_args, const char** args)
{
	if (!Core::initialize())
	{
		return Core::FAIL;
	}

	compile({&Core::get_proc("/obj/proc/jit_test_compiled_proc")});
	Core::get_proc("/obj/proc/jit_test_compiled_proc").hook(invoke_hook);
	return Core::SUCCESS;
}