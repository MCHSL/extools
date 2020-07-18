#include "Test.h"
#include "DMCompiler.h"
#include "JitContext.h"

#include "../../core/core.h"
#include "../../dmdism/instruction.h"
#include "../../dmdism/disassembly.h"

#include "../../third_party/robin_hood.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <array>

#define NOMINMAX
#include <Windows.h>


using namespace asmjit;
using namespace dmjit;

static std::ofstream jit_out("jit_out.txt");
static std::ofstream bytecode_out("bytecode.txt");
static std::ofstream blocks_out("blocks.txt");
static asmjit::JitRuntime rt;

struct JittedInfo
{
	void* code_base;
	bool needs_sleep;

	JittedInfo(void* cb, bool ns) : code_base(cb), needs_sleep(ns) {}
	JittedInfo() : code_base(nullptr), needs_sleep(true) {} //by default assume that a proc needs sleep
};

static robin_hood::unordered_map<unsigned int, JittedInfo> jitted_procs;

// How much to shift when using x86::ptr - [base + (offset << shift)]
unsigned int shift(const unsigned int n)
{
	return static_cast<unsigned int>(log2(n));
}

class SimpleErrorHandler : public asmjit::ErrorHandler
{
public:
	void handleError(asmjit::Error err, const char* message, asmjit::BaseEmitter* origin) override
	{
		this->err = err;
		jit_out << message << "\n";
	}

	asmjit::Error err = kErrorOk;
};

struct ProcBlock
{
	explicit ProcBlock(const unsigned int o) : offset(o) {}
	ProcBlock() : offset(0) {}
	std::vector<Instruction> contents;
	unsigned int offset;
	asmjit::Label label;
	//BlockNode* node;
};

static std::map<unsigned int, ProcBlock> split_into_blocks(Disassembly& dis, DMCompiler& dmc, size_t& locals_max_count, size_t& args_max_count, bool& need_sleep)
{
	std::map<unsigned int, ProcBlock> blocks;
	unsigned int current_block_offset = 0;
	blocks[current_block_offset] = ProcBlock(0);
	std::set<unsigned int> jump_targets;
	need_sleep = false;
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
			case AccessModifier::SUBVAR:
				if (i.acc_base.first == AccessModifier::LOCAL)
				{
					locals_max_count = std::max(locals_max_count, i.acc_base.second + 1);
				}
				else if(i.acc_base.first == AccessModifier::ARG)
				{
					args_max_count = std::max(args_max_count, i.acc_base.second + 1);
				}
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
					const auto split_point = std::find_if(victim.contents.begin(), victim.contents.end(), [target](Instruction& instr) { return instr.offset() == target; }); //find the target instruction
					ProcBlock new_block = ProcBlock(target);
					new_block.contents = std::vector<Instruction>(split_point, victim.contents.end());
					victim.contents.erase(split_point, victim.contents.end()); //split
					blocks[target] = new_block;
				}
			}
			current_block_offset = i.offset()+i.size();
			blocks[current_block_offset] = ProcBlock(current_block_offset);
		}
		else if (i == Bytecode::SLEEP)
		{
			need_sleep = true;
			current_block_offset = i.offset() + i.size();
			blocks[current_block_offset] = ProcBlock(current_block_offset);
		}
		else if (i == Bytecode::CALLGLOB)
		{
			if(!need_sleep)
			{
				auto jitted_it = jitted_procs.find(i.bytes().at(2));
				if(jitted_it != jitted_procs.end())
				{
					need_sleep = jitted_it->second.needs_sleep;
				}
			}
			current_block_offset = i.offset() + i.size();
			blocks[current_block_offset] = ProcBlock(current_block_offset);
		}
		else if (i == Bytecode::CALL || i == Bytecode::CALLNR)
		{
			need_sleep = true;
			current_block_offset = i.offset() + i.size();
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
	blocks_out << std::endl;

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

static void print_jit_runtime(const char* err)
{
	Core::alert_dd(err);
}

static void Emit_Abort(DMCompiler& dmc, const char* message)
{
	//dmc.clearStack();
	InvokeNode* call;
	dmc.invoke(&call, reinterpret_cast<uint32_t>(print_jit_runtime), FuncSignatureT<void, char*>());
	call->setArg(0, imm(message));
	dmc.pushStack(imm(NULL_D), imm(0));
	dmc.doReturn(true);
}

static void Emit_PushInteger(DMCompiler& dmc, float not_an_integer)
{
	dmc.pushStack(Imm(DataType::NUMBER), Imm(EncodeFloat(not_an_integer)));
}

static void Emit_Pop(DMCompiler& dmc)
{
	dmc.popStack();
}

static void Emit_PushValue(DMCompiler& dmc, DataType type, unsigned int value, unsigned int value2 = 0)
{
	dmc.pushStack(Imm(type), Imm(value));
}

static void Emit_Return(DMCompiler& dmc)
{
	dmc.doReturn();
}

static unsigned int add_strings(unsigned int str1, unsigned int str2)
{
	return Core::GetStringId(Core::GetStringFromId(str1) + Core::GetStringFromId(str2));
}

static void doStringAddition(DMCompiler& dmc, const Variable lhs, const Variable rhs, const Variable result)
{
	InvokeNode* call;
	dmc.invoke(&call, reinterpret_cast<uint32_t>(add_strings), FuncSignatureT<unsigned int, unsigned int, unsigned int>());
	call->setArg(0, lhs.Value);
	call->setArg(1, rhs.Value);
	call->setRet(0, result.Value);
	dmc.mov(result.Type, Imm(DataType::STRING));
}

static void doMathOperation(DMCompiler& dmc, const Bytecode op_type, const Variable lhs, const Variable rhs, const Variable result)
{
	const auto xmm0 = dmc.newXmm("lhs");
	const auto xmm1 = dmc.newXmm("rhs");

	if (lhs.Value.isImm())
	{
		const auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(lhs.Value.as<Imm>().value()));
		dmc.movd(xmm0, data);
	}
	else
	{
		dmc.movd(xmm0, lhs.Value.as<x86::Gp>());
	}

	if (rhs.Value.isImm())
	{
		const auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(rhs.Value.as<Imm>().value()));
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
	default:
		Core::Alert("Unknown math operation");
	}

	dmc.mov(result.Type, NUMBER);
	dmc.movd(result.Value, xmm0);
}

static void Emit_StringAddition(DMCompiler& dmc)
{
	const auto [lhs, rhs] = dmc.popStack<2>();
	const auto result = dmc.pushStack();
	doStringAddition(dmc, lhs, rhs, result);
}

static void Emit_MathOp(DMCompiler& dmc, const Bytecode op_type)
{
	auto [lhs, rhs] = dmc.popStack<2>();
	const Variable result = dmc.pushStack();
	doMathOperation(dmc, op_type, lhs, rhs, result);
}

static void Emit_GenericBinaryOp(DMCompiler& dmc, Bytecode op_type)
{
	auto [lhs, rhs] = dmc.popStack<2>();
	const Variable result = dmc.pushStack(Imm(DataType::NUMBER), Imm(0));
	const auto type_comparator = dmc.newUInt32("type_comparator");
	const auto done_adding_strings = dmc.newLabel();
	const auto invalid_arguments = dmc.newLabel();

	// At least one of the operands to cmp needs to be a register.
	if(!lhs.Type.isReg())
	{
		dmc.mov(type_comparator, lhs.Type);
		dmc.cmp(type_comparator, rhs.Type);
	}
	else if(!rhs.Type.isReg())
	{
		dmc.mov(type_comparator, rhs.Type);
		dmc.cmp(type_comparator, lhs.Type);
	}
	else
	{
		dmc.cmp(lhs.Type, rhs.Type);
	}
	dmc.jne(invalid_arguments);

	if (op_type == Bytecode::ADD)
	{
		const auto notstring = dmc.newLabel();
		
		dmc.mov(type_comparator, lhs.Type);
		dmc.cmp(type_comparator, Imm(DataType::STRING));
		dmc.jne(notstring);

		dmc.mov(type_comparator, rhs.Type);
		dmc.cmp(type_comparator, Imm(DataType::STRING));
		dmc.jne(notstring);
		
		doStringAddition(dmc, lhs, rhs, result);
		dmc.jmp(done_adding_strings);
		
		dmc.bind(notstring);
	}

	doMathOperation(dmc, op_type, lhs, rhs, result);
	dmc.jmp(done_adding_strings);
	dmc.bind(invalid_arguments);
	Emit_Abort(dmc, "Runtime in JIT compiled function: Invalid operand types for binary operation");
	dmc.bind(done_adding_strings);
}

static void Emit_BinaryOp(DMCompiler& dmc, Bytecode op_type, DataType optimize_for = DataType::NULL_D)
{
	if (op_type == ADD && optimize_for == STRING)
	{
		Emit_StringAddition(dmc);
		return;
	}
	if(optimize_for == NUMBER)
	{
		Emit_MathOp(dmc, op_type);
		return;
	}
	Emit_GenericBinaryOp(dmc, op_type);
}

static void Emit_CallGlobal(DMCompiler& dmc, uint32_t arg_count, uint32_t proc_id)
{
	x86::Mem args = dmc.newStack(sizeof(Value) * arg_count, 4);
	args.setSize(sizeof(uint32_t));
	uint32_t arg_i = arg_count;
	while (arg_i--)
	{
		Variable var = dmc.popStack();

		args.setOffset((uint64_t)arg_i * sizeof(Value) + offsetof(Value, type));
		if (var.Type.isImm())
		{
			//auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(var.Type.as<Imm>().value()));
			dmc.mov(args, var.Type.as<Imm>());
		}
		else
		{
			dmc.mov(args, var.Type.as<x86::Gp>());
		}

		args.setOffset((uint64_t)arg_i * sizeof(Value) + offsetof(Value, value));
		if (var.Value.isImm())
		{
			//auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(var.Value.as<Imm>().value()));
			dmc.mov(args, var.Value.as<Imm>());
		}
		else
		{
			dmc.mov(args, var.Value.as<x86::Gp>());
		}		
	}
	args.resetOffset();

	// dmc.commitLocals();
	// dmc.commitStack();

	const auto args_ptr = dmc.newUIntPtr("call_global_args_ptr");
	dmc.lea(args_ptr, args);

	const Variable ret = dmc.pushStack();

	auto proc_it = jitted_procs.find(proc_id);
	if (proc_it != jitted_procs.end())
	{
		void* code_base = proc_it->second.code_base;
		InvokeNode* call;
		dmc.invoke(&call, reinterpret_cast<uint64_t>(JitEntryPoint), FuncSignatureT<asmjit::Type::I64, void*, int, Value*, int, int, int, int, JitContext*>());
		call->setArg(0, imm(code_base));
		call->setArg(1, imm(arg_count));
		call->setArg(2, args_ptr);
		call->setArg(3, imm(0));
		call->setArg(4, imm(0));
		const Variable usr = dmc.getFrameEmbeddedValue(offsetof(ProcStackFrame, usr));
		call->setArg(5, usr.Type);
		call->setArg(6, usr.Value);
		call->setArg(7, dmc.getJitContext());
		call->setRet(0, ret.Type);
		call->setRet(1, ret.Value);
	}
	else
	{
		InvokeNode* call;
		dmc.invoke(&call, reinterpret_cast<uint64_t>(CallGlobalProc), FuncSignatureT<asmjit::Type::I64, int, int, int, int, int, int, int, int*, int, int, int>());
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
		call->setRet(0, ret.Type);
		call->setRet(1, ret.Value);
	}

}

static DMListIterator* create_iterator(ProcStackFrame* psf, int list_id)
{
	auto* const iter = new DMListIterator();
	RawList* list = GetListPointerById(list_id);
	iter->elements = new Value[list->length];
	std::copy(list->vector_part, list->vector_part + list->length, iter->elements);
	iter->length = list->length;
	iter->current_index = -1; // -1 because the index is immediately incremented by ITERNEXT
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
	const auto list = dmc.popStack();
	const auto new_iter = dmc.newUIntPtr("new iterator");
	const auto stack = dmc.getStackFramePtr();
	InvokeNode* create_iter;
	dmc.invoke(&create_iter, reinterpret_cast<uint64_t>(create_iterator), FuncSignatureT<int*, int*, int>());
	create_iter->setArg(0, stack);
	create_iter->setArg(1, list.Value);
	create_iter->setRet(0, new_iter);
	dmc.setCurrentIterator(new_iter);
}

static void Emit_Iterpop(DMCompiler& dmc)
{
	const auto prev_iter = dmc.newUIntPtr("previous iterator");
	InvokeNode* pop_iter;
	dmc.invoke(&pop_iter, reinterpret_cast<uint64_t>(pop_iterator), FuncSignatureT<int*, int*>());
	pop_iter->setArg(0, dmc.getStackFramePtr());
	pop_iter->setRet(0, prev_iter);
	dmc.setCurrentIterator(prev_iter);
}

// DM bytecode pushes the next value to the stack, immediately sets a local variable to it then checks to zero flag.
// Doing it the same way by compiling the next few instructions would screw up our zero flag, so we do all of it here.
static void Emit_Iternext(DMCompiler& dmc, const Instruction& setvar, const Instruction& jmp, const std::map<unsigned int, ProcBlock>& blocks)
{
	const auto current_iter = dmc.getCurrentIterator();
	const auto reg = dmc.newUInt32("list_len_comparator");
	dmc.mov(reg, x86::dword_ptr(current_iter, offsetof(DMListIterator, current_index)));
	dmc.inc(reg);
	dmc.commitStack();
	dmc.cmp(reg, x86::dword_ptr(current_iter, offsetof(DMListIterator, length)));
	dmc.jge(blocks.at(jmp.bytes().at(1)).label);
	dmc.mov(x86::dword_ptr(current_iter, offsetof(DMListIterator, current_index)), reg);
	const auto elements = dmc.newUIntPtr("iterator_elements");
	dmc.mov(elements, x86::dword_ptr(current_iter, offsetof(DMListIterator, elements)));
	const auto type = dmc.newUInt32("current_iter_type");
	dmc.mov(type, x86::dword_ptr(elements, reg, shift(sizeof(Value))));
	const auto value = dmc.newUInt32("current_iter_value");
	dmc.mov(value, x86::dword_ptr(elements, reg, shift(sizeof(Value)), offsetof(Value, value)));
	dmc.setLocal(setvar.bytes().at(2), Variable{type, value});
}

static void Emit_Jump(DMCompiler& dmc, uint32_t target, std::map<unsigned int, ProcBlock>& blocks)
{
	dmc.jump(blocks.at(target).label);
}

static void Emit_GetListElement(DMCompiler& dmc)
{
	const auto [container, key] = dmc.popStack<2>();
	const auto ret = dmc.pushStack();
	InvokeNode* getelem;
	dmc.invoke(&getelem, reinterpret_cast<uint32_t>(GetAssocElement), FuncSignatureT<asmjit::Type::I64, int, int, int, int>());
	getelem->setArg(0, container.Type.as<x86::Gp>());
	getelem->setArg(1, container.Value.as<x86::Gp>());
	getelem->setArg(2, key.Type.as<x86::Gp>());
	getelem->setArg(3, key.Value.as<x86::Gp>());
	getelem->setRet(0, ret.Type);
	getelem->setRet(1, ret.Value);
}

static void Emit_SetListElement(DMCompiler& dmc)
{
	const auto [container, key, value] = dmc.popStack<3>();
	InvokeNode* setelem;
	dmc.invoke(&setelem, reinterpret_cast<uint32_t>(SetAssocElement), FuncSignatureT<void, int, int, int, int, int, int>());
	setelem->setArg(0, container.Type.as<x86::Gp>());
	setelem->setArg(1, container.Value.as<x86::Gp>());
	setelem->setArg(2, key.Type.as<x86::Gp>());
	setelem->setArg(3, key.Value.as<x86::Gp>());
	setelem->setArg(4, value.Type.as<x86::Gp>());
	setelem->setArg(5, value.Value.as<x86::Gp>());
}

trvh create_list(Value* elements, unsigned int num_elements)
{
	List l;
	for (size_t i = 0; i < num_elements; i++)
	{
		l.append(elements[i]);
	}
	//IncRefCount(0x0F, l.id);
	return l;
}

static void Emit_CreateList(DMCompiler& dmc, const uint64_t num_elements)
{
	auto args = dmc.newStack(sizeof(Value) * num_elements, 4);
	for (size_t i = 0; i < num_elements; i++)
	{
		auto arg = dmc.popStack();
		args.setOffset((num_elements - i - 1) * sizeof(Value) + offsetof(Value, type));
		dmc.mov(args, arg.Type);
		args.setOffset((num_elements - i - 1) * sizeof(Value) + offsetof(Value, value));
		dmc.mov(args, arg.Value);
	}
	args.resetOffset();
	const auto args_ptr = dmc.newUInt32("create_list_args");
	dmc.lea(args_ptr, args);
	const auto result = dmc.pushStack(Imm(DataType::LIST), Imm(0xFFFF)); // Seems like pushStack needs to happen after newStack?
	InvokeNode* cl;
	dmc.invoke(&cl, reinterpret_cast<uint32_t>(create_list), FuncSignatureT<asmjit::Type::I64, Value*, unsigned int>());
	cl->setArg(0, args_ptr);
	cl->setArg(1, Imm(num_elements));
	cl->setRet(0, result.Type);
	cl->setRet(1, result.Value);

}

/*static void Emit_Output(DMCompiler& dmc)
{

}*/

static void Emit_End(DMCompiler& dmc)
{
	dmc.pushStackRaw(dmc.getDot());
	dmc.doReturn();
}

static void Emit_Sleep(DMCompiler& dmc)
{
	dmc.doSleep();
}

static void Emit_FieldRead(DMCompiler& dmc, const Instruction& instr)
{
	const auto& base = instr.acc_base;
	Variable base_var;
	switch (base.first)
	{
	case AccessModifier::ARG:
		base_var = dmc.getArg(base.second);
		break;
	case AccessModifier::LOCAL:
		base_var = dmc.getLocal(base.second);
		break;
	case AccessModifier::SRC:
		base_var = dmc.getSrc();
		break;
	default:
		Core::Alert("Unknown access modifier in jit get_variable");
		break;
	}

	dmc.setCached(base_var);

	for (unsigned int name : instr.acc_chain)
	{
		auto* call = dmc.call((uint64_t)GetVariable, FuncSignatureT<asmjit::Type::I64, int, int, int>());
		call->setArg(0, base_var.Type);
		call->setArg(1, base_var.Value);
		call->setArg(2, imm(name));
		call->setRet(0, base_var.Type);
		call->setRet(1, base_var.Value);
	}


	dmc.pushStackRaw(base_var);
}

static void Emit_FieldWrite(DMCompiler& dmc, const Instruction& instr)
{
	Variable new_value = dmc.popStack();
	const auto& base = instr.acc_base;
	Variable base_var;
	switch (base.first)
	{
	case AccessModifier::ARG:
		base_var = dmc.getArg(base.second);
		break;
	case AccessModifier::LOCAL:
		base_var = dmc.getLocal(base.second);
		break;
	case AccessModifier::SRC:
		base_var = dmc.getSrc();
		break;
	default:
		if(base.first > 64000)
		{
			Core::Alert("Unknown access modifier in jit get_variable");
		}
		break;
	}

	dmc.setCached(base_var);

	for (unsigned int i=0; i<instr.acc_chain.size() - 1; i++)
	{
		const unsigned int name = instr.acc_chain.at(i);
		auto* call = dmc.call((uint64_t)GetVariable, FuncSignatureT<asmjit::Type::I64, int, int, int>());
		call->setArg(0, base_var.Type);
		call->setArg(1, base_var.Value);
		call->setArg(2, imm(name));
		call->setRet(0, base_var.Type);
		call->setRet(1, base_var.Value);
	}

	const unsigned int final_field_name = instr.acc_chain.back();
	auto* call = dmc.call((uint64_t)SetVariable, FuncSignatureT<void, int, int, int, int, int>());
	call->setArg(0, base_var.Type);
	call->setArg(1, base_var.Value);
	call->setArg(2, imm(final_field_name));
	call->setArg(3, new_value.Type);
	call->setArg(4, new_value.Value);

}

static void Emit_GetCachedField(DMCompiler& dmc, const unsigned int name)
{
	const Variable result = dmc.pushStack();
	const Variable cached = dmc.getCached();
	auto* call = dmc.call((uint64_t)GetVariable, FuncSignatureT<asmjit::Type::I64, int, int, int>());
	call->setArg(0, cached.Type);
	call->setArg(1, cached.Value);
	call->setArg(2, imm(name));
	call->setRet(0, result.Type);
	call->setRet(1, result.Value);
}

static bool Emit_Block(DMCompiler& dmc, ProcBlock& block, std::map<unsigned int, ProcBlock>& blocks)
{
	/*block.node = */
	dmc.addBlock(block.label);

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
			Emit_BinaryOp(dmc, (Bytecode)instr.bytes()[0]);
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
			case AccessModifier::SUBVAR:
				Emit_FieldWrite(dmc, instr);
				break;
			default:
				Core::Alert("Failed to assemble setvar ");
				break;
			}
			break;
		case Bytecode::GETVAR:
			switch (instr.bytes()[1])
			{
			case AccessModifier::LOCAL:
				dmc.pushStackRaw(dmc.getLocal(instr.bytes()[2]));
				break;
			case AccessModifier::ARG:
				dmc.pushStackRaw(dmc.getArg(instr.bytes()[2]));
				break;
			case AccessModifier::SRC:
				dmc.pushStackRaw(dmc.getSrc());
				break;
			case AccessModifier::DOT:
				dmc.pushStackRaw(dmc.getDot());
				break;
			case AccessModifier::SUBVAR:
				Emit_FieldRead(dmc, instr);
				break;
			default:
				if(instr.bytes()[1] > 64000)
				{
					Core::Alert("Failed to assemble getvar");
				}
				else
				{
					Emit_GetCachedField(dmc, instr.bytes()[1]);
				}
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
		case Bytecode::CREATELIST:
			jit_out << "Assembling create list" << std::endl;
			Emit_CreateList(dmc, instr.bytes()[1]);
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
			break; // What this opcode does is already handled in ITERLOAD
		case Bytecode::ITERATOR_POP:
			jit_out << "Assembling iterator pop" << std::endl;
			Emit_Iterpop(dmc);
			break;
		case Bytecode::JMP:
		case Bytecode::JMP2:
			jit_out << "Assembling jump" << std::endl;
			Emit_Jump(dmc, instr.bytes()[1], blocks);
			break;
		case Bytecode::SLEEP:
			jit_out << "Assembling sleep" << std::endl;
			Emit_Sleep(dmc);
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

static SuspendPtr oSuspend;
static ProcConstants* hSuspend(ExecutionContext* ctx, int unknown)
{
	const uint32_t proc_id = ctx->constants->proc_id;
	if (proc_id == Core::get_proc("/proc/jit_wrapper").id)
	{
		auto* const jc = reinterpret_cast<JitContext*>(ctx->constants->args[1].value);
		jc->suspended = true;
	}
	return oSuspend(ctx, unknown);
}

static ExecutionContext* __fastcall fuck(ExecutionContext* dmctx)
{
	ProcConstants* const pc = dmctx->constants;
	// This flag might actually be something like "suspendable", doubt it though
	dmctx->paused = true;
	auto* const jctx = reinterpret_cast<JitContext*>(pc->args[1].value);
	jctx->suspended = false;
	// The first 2 arguments are base and context, we pass the pointer to the third arg onwards.
	const unsigned int args_len = std::max(0, pc->arg_count - 2);
	const Value retval = JitEntryPoint(reinterpret_cast<void*>(pc->args[0].value), args_len, pc->args + 2, pc->src, pc->usr, reinterpret_cast<JitContext*>(pc->args[1].value));
	dmctx->stack_size++;
	dmctx->stack[dmctx->stack_size - 1] = retval;
	dmctx->dot = retval;
	return dmctx;
}

static uint32_t wrapper_id = 0;

__declspec(naked) ExecutionContext* just_before_execution_hook()
{
	__asm {
		mov eax, DWORD PTR [Core::current_execution_context_ptr]
		mov eax, DWORD PTR [eax]
		mov edx, DWORD PTR [eax]
		mov ecx, DWORD PTR [wrapper_id]
		cmp DWORD PTR [edx], ecx
		jne yeet
		mov ecx, eax
		call fuck // fastcall - the context must be passed in ecx
		xor edx, edx
		yeet:
		ret
	}
	// xoring a register sets the zero flag. This hook is preceded by a TEST and followed by a JE which jumps over the bytecode interpreting part.
	// So we're pretty much skipping the entire bytecode bit and go faster.
}

static void hook_resumption()
{
	wrapper_id = Core::get_proc("/proc/jit_wrapper").id;
	oSuspend = Core::install_hook(Suspend, hSuspend);

	DWORD old_prot;
	char* remember_to_return_context = (char*)Pocket::Sigscan::FindPattern("byondcore.dll", "A1 ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? 66 FF 40 14 EB 18 8B 00 80 78 10 22 A1 ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? EB 05 A1 ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 89 ?? ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 89");
	VirtualProtect(remember_to_return_context, 5, PAGE_READWRITE, &old_prot);
	remember_to_return_context[0] = (char)0xE8; //CALL
	*(int*)(remember_to_return_context + 1) = (int)&just_before_execution_hook - (int)remember_to_return_context - 5;
	VirtualProtect(remember_to_return_context, 5, old_prot, &old_prot);

	remember_to_return_context = (char*)Pocket::Sigscan::FindPattern("byondcore.dll", "A1 ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 89 ?? ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 89 ?? ?? ?? ?? ?? 0F B7 48 14 8B 78 10 8B F1 8B 14 B7 81 FA");
	VirtualProtect(remember_to_return_context, 5, PAGE_READWRITE, &old_prot);
	remember_to_return_context[0] = (char)0xE8; //CALL
	*(int*)(remember_to_return_context + 1) = (int)&just_before_execution_hook - (int)remember_to_return_context - 5;
	VirtualProtect(remember_to_return_context, 5, old_prot, &old_prot);
	
	remember_to_return_context = (char*)Pocket::Sigscan::FindPattern("byondcore.dll", "A1 ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? EB 05 E8 ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 89 ?? ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 89 ?? ?? ?? ?? ?? 0F B7 48 14 8B 78 10 8B F1 8B 14 B7 81 FA ?? ?? ?? ?? 0F 87 ?? ?? ?? ?? FF 24 ?? ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 8D 41 01 0F B7 C8");
	VirtualProtect(remember_to_return_context, 5, PAGE_READWRITE, &old_prot);
	remember_to_return_context[0] = (char)0xE8; //CALL
	*(int*)(remember_to_return_context + 1) = (int)&just_before_execution_hook - (int)remember_to_return_context - 5;
	VirtualProtect(remember_to_return_context, 5, old_prot, &old_prot);
}

trvh JitEntryPoint(void* code_base, const unsigned int args_len, Value* const args, const Value src, const Value usr, JitContext* ctx)
{
	if (!ctx)
	{
		ctx = new JitContext();
	}
	
	const Proc code = reinterpret_cast<Proc>(code_base);

	const ProcResult res = code(ctx, args_len, args, src, usr);

	switch (res)
	{
	case ProcResult::Success:
	{
		/*if (ctx->CountFrame() != 1)
		{
			__debugbreak();
			break;
		}*/
		const Value return_value = *--ctx->stack_top;
		if (!ctx->stack_frame)
		{
			// We've returned from the only proc in this context's stack, so it is no longer needed.
			delete ctx;
		}
		return return_value;
	}
	case ProcResult::Yielded:
	{
		const Value dot = *--ctx->stack_top;
		// No need to do anything here, the JitContext is already marked as suspended
		if (!ctx->suspended)
		{
			// Let's check though, just to make sure
			__debugbreak();
		}
		return dot;
	}
	case ProcResult::Sleeping:
	{
		const Value dot = *--ctx->stack_top;
		const Value sleep_time = *--ctx->stack_top;
		// We are inside of a DM proc wrapper. It will be suspended by this call,
		// and the suspension will propagate to parent jit calls.
		ProcConstants* suspended = Suspend(Core::get_context(), 0);
		suspended->time_to_resume = static_cast<unsigned int>(sleep_time.valuef / Value::World().get("tick_lag").valuef);
		StartTiming(suspended);
		return dot;
	}
	}
	__debugbreak();
	return Value::Null();
}

static void compile(std::vector<Core::Proc*> procs, DMCompiler* parent_compiler = nullptr)
{
	FILE* fuck;
	fopen_s(&fuck, "asm.txt", "w");
	asmjit::FileLogger logger(fuck);
	logger.addFlags(FormatOptions::kFlagRegCasts | FormatOptions::kFlagExplainImms | FormatOptions::kFlagDebugPasses | FormatOptions::kFlagDebugRA | FormatOptions::kFlagAnnotations);
	SimpleErrorHandler eh;
	asmjit::CodeHolder code;
	code.init(rt.environment());
	code.setLogger(&logger);
	code.setErrorHandler(&eh);
	DMCompiler dmc(code);

	std::vector<Label> entrypoints;
	entrypoints.reserve(procs.size());

	for (auto& proc : procs)
	{
		Disassembly dis = proc->disassemble();
		bytecode_out << "BEGIN " << proc->name << '\n';
		for (const Instruction& i : dis)
		{
			bytecode_out << i.bytes_str() << std::endl;
		}
		bytecode_out << "END " << proc->name << '\n';

		size_t locals_count = 0; // Defaulting to 1 because asmjit does not like empty vectors apparently (causes an assertion error)
		size_t args_count = 0;
		bool need_sleep = false;
		auto blocks = split_into_blocks(dis, dmc, locals_count, args_count, need_sleep);

		ProcNode* node = dmc.addProc(locals_count, args_count, need_sleep);
		entrypoints.push_back(node->_entryPoint);
		for (auto& [k, v] : blocks)
		{
			Emit_Block(dmc, v, blocks);
		}
		dmc.endProc();
	}

	dmc.finalize();
	void* code_base = nullptr;
	rt.add(&code_base, &code);

	for (size_t i = 0; i < procs.size(); i++)
	{
		void* entrypoint = reinterpret_cast<void*>(reinterpret_cast<uint32_t>(code_base) + code.labelOffset(entrypoints.at(i)));
		jitted_procs.emplace(procs.at(i)->id, JittedInfo(entrypoint, false ));
		jit_out << procs.at(i)->name << ": " << entrypoint << std::endl;
	}
	fflush(fuck);
}

void* compile_one(Core::Proc& proc)
{
	compile({ &proc });
	return jitted_procs[proc.id].code_base; //todo
}

extern "C" EXPORT const char* jit_test(int n_args, const char** args)
{
	if (!Core::initialize())
	{
		return Core::FAIL;
	}

	hook_resumption();
	//compile({&Core::get_proc("/proc/jit_test_compiled_proc"), &Core::get_proc("/proc/recursleep")});
	//Core::get_proc("/proc/tiny_proc").jit();
	Core::get_proc("/proc/jit_test_compiled_proc").jit();
	Core::get_proc("/proc/jit_wrapper").set_bytecode({ Bytecode::RET, Bytecode::RET, Bytecode::RET });
	return Core::SUCCESS;
}