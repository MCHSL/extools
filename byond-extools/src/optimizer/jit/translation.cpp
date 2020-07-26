#include "translation.h"
#include "../../core/core.h"
#include "DMCompiler.h"
#include "JitContext.h"
#include "analysis.h"
#include "jit_runtime.h"
#include "../../third_party/robin_hood.h"
#include <set>
#include <thread>
#include <unordered_set>

using namespace asmjit;
using namespace dmjit;

robin_hood::unordered_map<unsigned int, JittedInfo> jitted_procs;

static std::unordered_set<unsigned int> unjitted_procs;
static std::unordered_set<unsigned int> procs_being_compiled;

// How much to shift when using x86::ptr - [base + (offset << shift)]
unsigned int shift(const unsigned int n)
{
	return static_cast<unsigned int>(log2(n));
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

std::string get_parent_type(const std::string& typepath)
{
	Value path = TextToPath(Core::GetStringId(typepath));
	if (path.type == MOB_TYPEPATH)
	{
		path.value = *MobTableIndexToGlobalTableIndex(path.value);
	}
	const TType* type = GetTypeById(path.value);
	const unsigned int parent_type = type->parentTypeIdx;
	return Core::GetStringFromId(GetTypeById(parent_type)->path);
}

static x86::Gp alloc_arguments_from_stack(DMCompiler& dmc, unsigned int arg_count)
{
	x86::Mem args = dmc.newStack(sizeof(Value) * arg_count, 4);
	args.setSize(sizeof(uint32_t));
	if(arg_count == 0xFFFF)
	{
		arg_count = 1; // Argument count of 0xFFFF means there's a list of args on the stack and we need to pass that.
	}
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
	x86::Gp args_ptr = dmc.newUIntPtr("call_args_ptr");
	dmc.lea(args_ptr, args);
	return args_ptr;
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
	if (!lhs.Type.isReg())
	{
		dmc.movzx(type_comparator, lhs.Type);
		dmc.cmp(type_comparator.r8Lo(), rhs.Type.r8Lo());
	}
	else if (!rhs.Type.isReg())
	{
		dmc.mov(type_comparator, rhs.Type);
		dmc.cmp(type_comparator.r8Lo(), lhs.Type.r8Lo());
	}
	else
	{
		dmc.cmp(lhs.Type.r8Lo(), rhs.Type.r8Lo());
	}
	dmc.jne(invalid_arguments);

	if (op_type == Bytecode::ADD)
	{
		const auto notstring = dmc.newLabel();

		dmc.mov(type_comparator, lhs.Type);
		dmc.cmp(type_comparator.r8Lo(), Imm(DataType::STRING));
		dmc.jne(notstring);

		dmc.mov(type_comparator, rhs.Type);
		dmc.cmp(type_comparator.r8Lo(), Imm(DataType::STRING));
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
	if (optimize_for == NUMBER)
	{
		Emit_MathOp(dmc, op_type);
		return;
	}
	Emit_GenericBinaryOp(dmc, op_type);
}

static void Emit_CallGlobal(DMCompiler& dmc, uint32_t arg_count, uint32_t proc_id)
{
	const auto args_ptr = alloc_arguments_from_stack(dmc, arg_count);
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
	dmc.setLocal(setvar.bytes().at(2), Variable{ type, value });
}

static void Emit_Jump(DMCompiler& dmc, const uint32_t target, const std::map<unsigned int, ProcBlock>& blocks)
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

static void Emit_Output(DMCompiler& dmc)
{
	auto [target, outputee] = dmc.popStack<2>();
	InvokeNode* call;
	dmc.invoke(&call, (uint64_t)Output, FuncSignatureT<bool, char, int, int, int, char, int>());
	call->setArg(0, target.Type);
	call->setArg(1, target.Value);
	call->setArg(2, imm(0));
	call->setArg(3, imm(0));
	call->setArg(4, outputee.Type);
	call->setArg(5, outputee.Value);
}

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
	case 0:
		base_var = dmc.getCached();
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
	const Variable new_value = dmc.popStack();
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
	case 0:
		base_var = dmc.getCached();
		break;
	default:
		Core::Alert("Unknown access modifier in jit get_variable");
		break;
	}

	dmc.setCached(base_var);

	for (unsigned int i = 0; i < instr.acc_chain.size() - 1; i++)
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

static void Emit_SetCachedField(DMCompiler& dmc, const unsigned int name)
{
	const Variable value = dmc.popStack();
	const Variable cached = dmc.getCached();
	auto* call = dmc.call((uint64_t)SetVariable, FuncSignatureT<void, int, int, int, int, int>());
	call->setArg(0, cached.Type);
	call->setArg(1, cached.Value);
	call->setArg(2, imm(name));
	call->setArg(3, value.Type);
	call->setArg(4, value.Value);
}

unsigned int translate_proc_name(unsigned int datum_type, unsigned int datum_id, unsigned int name)
{
	Value type = Value((DataType)datum_type, datum_id).get("type");
	if(type.type == MOB_TYPEPATH)
	{
		type.value = *MobTableIndexToGlobalTableIndex(type.value);
	}
	std::string stringy_type = Core::GetStringFromId(GetTypeById(type.value)->path);
	const std::string proc_name = Core::GetStringFromId(name);
	while(stringy_type.find('/') != std::string::npos)
	{
		const Core::Proc* proc = Core::try_get_proc(stringy_type + "/" + proc_name);
		if (proc)
		{
			return proc->id;
		}
		stringy_type = get_parent_type(stringy_type);
	}
	return -1;
	/*unsigned int omegalul;
	return TranslateProcNameToProcId(0x02, name, 0, datum_type, datum_id, &omegalul, 1);*/
}

void* check_is_jitted(unsigned int proc_id)
{
	auto jit_it = jitted_procs.find(proc_id);
	if (jit_it != jitted_procs.end())
	{
		return jit_it->second.code_base;
	}
	return nullptr;
}

static void add_unjitted_proc(unsigned int proc_id)
{
	const auto ret = unjitted_procs.emplace(proc_id);
	if(ret.second)
	{
		const Core::Proc& proc = Core::get_proc(proc_id);
		//Core::Alert(proc.name);
		proc.jit();
	}
}

static void Emit_Call(DMCompiler& dmc, const Instruction& instr)
{
	const auto& bytes = instr.bytes();
	const unsigned int proc_selector = bytes.end()[-3];
	const unsigned int proc_identifier = bytes.end()[-2];
	const unsigned int arg_count = bytes.end()[-1];

	const auto args_ptr = alloc_arguments_from_stack(dmc, arg_count);


	const Variable result = dmc.pushStack();

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
	case 0:
		base_var = dmc.getCached();
		break;
	default:
		if (base.first > 64000)
		{
			Core::Alert("Unknown access modifier in jit get_variable");
		}
		break;
	}

	dmc.setCached(base_var);

	if(!instr.acc_chain.empty())
	{
		for (unsigned int i = 0; i < instr.acc_chain.size() - 1; i++)
		{
			const unsigned int name = instr.acc_chain.at(i);
			auto* call = dmc.call((uint64_t)GetVariable, FuncSignatureT<asmjit::Type::I64, int, int, int>());
			call->setArg(0, base_var.Type);
			call->setArg(1, base_var.Value);
			call->setArg(2, imm(name));
			call->setRet(0, base_var.Type);
			call->setRet(1, base_var.Value);
		}
	}

	const auto usr = dmc.getUsr();
	const auto proc_id = dmc.newUInt32("proc_id");

	if (proc_selector == ProcSelector::SRC_PROC_SPEC)
	{
		InvokeNode* call;
		dmc.invoke(&call, (uint64_t)translate_proc_name, FuncSignatureT<unsigned int, unsigned int, unsigned int, unsigned int>());
		call->setArg(0, base_var.Type);
		call->setArg(1, base_var.Value);
		call->setArg(2, imm(proc_identifier));
		call->setRet(0, proc_id);
	}
	else
	{
		dmc.mov(proc_id, imm(proc_identifier));
	}

	const auto not_jitted = dmc.newLabel();
	const auto done = dmc.newLabel();

	const auto code_base = dmc.newUIntPtr("code_base");

	InvokeNode* check_jit;
	dmc.invoke(&check_jit, reinterpret_cast<uint64_t>(check_is_jitted), FuncSignatureT<void*, unsigned int>());
	check_jit->setArg(0, proc_id);
	check_jit->setRet(0, code_base);

	dmc.test(code_base, code_base);
	dmc.je(not_jitted);
	InvokeNode* jitcall;
	dmc.invoke(&jitcall, reinterpret_cast<uint64_t>(JitEntryPoint), FuncSignatureT<asmjit::Type::I64, void*, int, Value*, int, int, int, int, JitContext*>());
	jitcall->setArg(0, code_base);
	jitcall->setArg(1, imm(arg_count));
	jitcall->setArg(2, args_ptr);
	jitcall->setArg(3, base_var.Type);
	jitcall->setArg(4, base_var.Value);
	jitcall->setArg(5, usr.Type);
	jitcall->setArg(6, usr.Value);
	jitcall->setArg(7, dmc.getJitContext());
	jitcall->setRet(0, result.Type);
	jitcall->setRet(1, result.Value);

	dmc.jmp(done);
	dmc.bind(not_jitted);
	auto* add_unjitted = dmc.call((uint64_t)add_unjitted_proc, FuncSignatureT<void, unsigned int>());
	add_unjitted->setArg(0, proc_id);
	auto* call = dmc.call((uint64_t)CallGlobalProc, FuncSignatureT<asmjit::Type::I64, int, int, int, int, int, int, Value*, int, int, int, int>());
	call->setArg(0, usr.Type);
	call->setArg(1, usr.Value);
	call->setArg(2, imm(0x2));
	call->setArg(3, proc_id);
	call->setArg(4, imm(0));
	call->setArg(5, base_var.Type);
	call->setArg(6, base_var.Value);
	call->setArg(7, args_ptr);
	call->setArg(8, imm(arg_count));
	call->setArg(9, imm(0));
	call->setArg(10, imm(0));
	call->setRet(0, result.Type);
	call->setRet(1, result.Value);
	dmc.bind(done);

}

static void Emit_GetFlag(DMCompiler& dmc)
{
	const auto done = dmc.newLabel();
	const auto flag = dmc.getFlag();
	const auto stack_crap = dmc.pushStack(imm(NUMBER), imm(0));
	dmc.test(flag, flag);
	dmc.jz(done); // we push FALSE by default so no need to update it.
	dmc.mov(stack_crap.Value, imm(0x3f800000)); // Floating point representation of 1 (TRUE)
	dmc.bind(done);
}

static void Emit_Comparison(DMCompiler& dmc, const Bytecode comp)
{
	const auto [lhs, rhs] = dmc.popStack<2>();
	const auto stack_crap = dmc.pushStack(imm(NUMBER), imm(0));

	if(comp == Bytecode::TEQ || comp == Bytecode::TNE)
	{
		const auto not_equal = dmc.newLabel();
		const auto done = dmc.newLabel();
		dmc.cmp(lhs.Type.r8Lo(), rhs.Type.r8Lo());
		dmc.jne(not_equal);
		dmc.cmp(lhs.Value, rhs.Value);
		dmc.jne(not_equal);
		dmc.setFlag();
		dmc.mov(stack_crap.Value, imm(0x3F800000));
		dmc.jmp(done);
		dmc.bind(not_equal);
		dmc.unsetFlag();
		dmc.mov(stack_crap.Value, imm(0));
		dmc.bind(done);
	}
	else
	{
		const auto does_not_pass = dmc.newLabel();
		const auto done = dmc.newLabel();

		// We're only going to allow comparing numbers.
		dmc.cmp(lhs.Type.r8Lo(), imm(NUMBER));
		dmc.jne(does_not_pass);
		dmc.cmp(rhs.Type.r8Lo(), imm(NUMBER));
		dmc.jne(does_not_pass);
		dmc.cmp(lhs.Value, rhs.Value);
		switch(comp)
		{
		case Bytecode::TL:
			dmc.jge(does_not_pass);
			break;
		case Bytecode::TLE:
			dmc.jg(does_not_pass);
			break;
		case Bytecode::TG:
			dmc.jle(does_not_pass);
			break;
		case Bytecode::TGE:
			dmc.jl(does_not_pass);
			break;
		default:
			Core::Alert("UNKNOWN COMPARISON");
			dmc.jmp(does_not_pass);
			break;
		}
		dmc.setFlag();
		dmc.mov(stack_crap.Value, imm(0x3F800000));
		dmc.jmp(done);
		dmc.bind(does_not_pass);
		dmc.unsetFlag();
		dmc.mov(stack_crap.Value, imm(0));
		dmc.bind(done);
	}
}

static void check_truthiness(DMCompiler& dmc, const Variable& val, const Label& truthy, const Label& falsey)
{
	dmc.cmp(val.Value, imm(0)); // If the value is not 0, then it must be truthy.
	dmc.jne(truthy);
	dmc.cmp(val.Type.r8Lo(), imm(0)); // If it has a value of zero but is not NULL, a string or a number, it's truthy.
	dmc.je(falsey);
	dmc.cmp(val.Type.r8Lo(), imm(STRING));
	dmc.je(falsey);
	dmc.cmp(val.Type.r8Lo(), imm(NUMBER));
	dmc.je(falsey);
	dmc.jmp(truthy);
}

static void Emit_Test(DMCompiler& dmc)
{
	const auto val = dmc.popStack();
	const auto truthy = dmc.newLabel();
	const auto falsey = dmc.newLabel();
	const auto done = dmc.newLabel();
	check_truthiness(dmc, val, truthy, falsey);
	dmc.bind(falsey);
	dmc.unsetFlag();
	dmc.jmp(done);
	dmc.bind(truthy);
	dmc.setFlag();
	dmc.bind(done);
}

static void Emit_ConditionalJump(DMCompiler& dmc, Bytecode cond, const uint32_t target, const std::map<unsigned int, ProcBlock>& blocks)
{
	const auto flag = dmc.getFlag();
	dmc.test(flag, flag);
	switch(cond)
	{
	case Bytecode::JUMP_FALSE:
	case Bytecode::JUMP_FALSE2:
		dmc.jz(blocks.at(target).label);
		break;
	case Bytecode::JUMP_TRUE:
	case Bytecode::JUMP_TRUE2:
		dmc.jnz(blocks.at(target).label); 
		break;
	default:
		break; // Stop complaining, resharper
	}
}

static void Emit_ConditionalChainJump(DMCompiler& dmc, Bytecode cond, const uint32_t target, const std::map<unsigned int, ProcBlock>& blocks)
{
	const auto lhs_result = dmc.popStack();
	const auto truthy = dmc.newLabel();
	const auto falsey = dmc.newLabel();
	const auto done = dmc.newLabel();
	check_truthiness(dmc, lhs_result, truthy, falsey);
	dmc.bind(falsey);
	if(cond == JMP_AND)
	{
		dmc.pushStackDirect(Variable{ imm(0).as<x86::Gp>(), imm(0).as<x86::Gp>() });
		dmc.jmp(blocks.at(target).label);
	}
	else
	{
		dmc.jmp(done);
	}
	dmc.bind(truthy);
	if(cond == JMP_OR)
	{
		dmc.pushStackDirect(Variable{ imm(NUMBER).as<x86::Gp>(), imm(0x3F800000).as<x86::Gp>() });
		dmc.jmp(blocks.at(target).label);
	}
	dmc.bind(done);
}

static void Emit_LogicalNot(DMCompiler& dmc)
{
	const auto val = dmc.popStack();
	const auto result = dmc.pushStack(imm(NUMBER), imm(0));
	const auto truthy = dmc.newLabel();
	const auto falsey = dmc.newLabel();
	check_truthiness(dmc, val, truthy, falsey);
	dmc.bind(falsey);
	dmc.mov(result.Value, imm(0x3f800000));
	dmc.bind(truthy); // We push FALSE by default so no need to do anything
}

static void Emit_IsType(DMCompiler& dmc)
{
	const auto [thing, type] = dmc.popStack<2>();
	const auto result = dmc.pushStack(imm(NUMBER), imm(0));
	InvokeNode* call;
	dmc.invoke(&call, (uint64_t)IsType, FuncSignatureT<bool, unsigned int, unsigned int, unsigned int, unsigned int>());
	call->setArg(0, thing.Type);
	call->setArg(1, thing.Value);
	call->setArg(2, type.Type);
	call->setArg(3, type.Value);
	call->setRet(0, result.Value);

	const auto done = dmc.newLabel();
	dmc.test(result.Value, result.Value);
	dmc.je(done);
	dmc.mov(result.Value, imm(0x3F800000));
	dmc.bind(done);
}

Core::Proc* find_parent_proc(const unsigned int proc_id)
{
	const Core::Proc& proc = Core::get_proc(proc_id);
	std::string proc_path = proc.name;
	const std::string& proc_name = proc.simple_name;

	const size_t proc_pos = proc_path.rfind('/');
	if (proc_pos != std::string::npos)
	{
		proc_path.erase(proc_pos);
	}
	while (proc_path.find('/') != std::string::npos)
	{
		proc_path = get_parent_type(proc_path);
		Core::Proc* p = Core::try_get_proc(proc_path + "/" + proc_name);
		if(p)
		{
			return p;
		}

	}
	return nullptr;
}

static void Emit_CallParent(DMCompiler& dmc, const unsigned int proc_id)
{
	const Core::Proc* const parent_proc = find_parent_proc(proc_id);
	Core::Alert(parent_proc->name);
	if(!parent_proc)
	{
		Core::Alert("Failed to locate parent proc");
	}
	const auto result = dmc.pushStack();

	const auto usr = dmc.getUsr();
	const auto src = dmc.getSrc();

	const auto not_jitted = dmc.newLabel();
	const auto done = dmc.newLabel();

	const auto code_base = dmc.newUIntPtr("code_base");

	const unsigned int arg_count = dmc.getArgCount();

	x86::Mem args = dmc.newStack(sizeof(Value) * arg_count, 4);
	args.setSize(sizeof(uint32_t));

	for(unsigned int i=0; i<arg_count; i++)
	{
		Variable var = dmc.getArg(i);

		args.setOffset((uint64_t)i * sizeof(Value) + offsetof(Value, type));
		dmc.mov(args, var.Type.as<x86::Gp>());

		args.setOffset((uint64_t)i * sizeof(Value) + offsetof(Value, value));
		dmc.mov(args, var.Value.as<x86::Gp>());
	}
	args.resetOffset();
	const x86::Gp args_ptr = dmc.newUIntPtr("call_parent_args_ptr");
	dmc.lea(args_ptr, args);

	InvokeNode* check_jit;
	dmc.invoke(&check_jit, reinterpret_cast<uint64_t>(check_is_jitted), FuncSignatureT<void*, unsigned int>());
	check_jit->setArg(0, imm(parent_proc->id));
	check_jit->setRet(0, code_base);

	dmc.test(code_base, code_base);
	dmc.jz(not_jitted);
	InvokeNode* jitcall;
	dmc.invoke(&jitcall, reinterpret_cast<uint64_t>(JitEntryPoint), FuncSignatureT<asmjit::Type::I64, void*, int, Value*, int, int, int, int, JitContext*>());
	jitcall->setArg(0, code_base);
	jitcall->setArg(1, imm(arg_count));
	jitcall->setArg(2, args_ptr);
	jitcall->setArg(3, src.Type);
	jitcall->setArg(4, src.Value);
	jitcall->setArg(5, usr.Type);
	jitcall->setArg(6, usr.Value);
	jitcall->setArg(7, dmc.getJitContext());
	jitcall->setRet(0, result.Type);
	jitcall->setRet(1, result.Value);

	dmc.jmp(done);
	dmc.bind(not_jitted);
	auto* add_unjitted = dmc.call((uint64_t)add_unjitted_proc, FuncSignatureT<void, unsigned int>());
	add_unjitted->setArg(0, imm(parent_proc->id));
	auto* call = dmc.call((uint64_t)CallGlobalProc, FuncSignatureT<asmjit::Type::I64, int, int, int, int, int, int, Value*, int, int, int, int>());
	call->setArg(0, usr.Type);
	call->setArg(1, usr.Value);
	call->setArg(2, imm(0x2));
	call->setArg(3, imm(parent_proc->id));
	call->setArg(4, imm(0));
	call->setArg(5, src.Type);
	call->setArg(6, src.Value);
	call->setArg(7, args_ptr);
	call->setArg(8, imm(arg_count));
	call->setArg(9, imm(0));
	call->setArg(10, imm(0));
	call->setRet(0, result.Type);
	call->setRet(1, result.Value);
	dmc.bind(done);
	
}

// For binary (2-argument) bitwise operations.
static void Emit_BitwiseOp(DMCompiler& dmc, Bytecode op)
{
	const auto [flhs, frhs] = dmc.popStack<2>();
	const auto result = dmc.pushStack(imm(NUMBER), imm(0));
	const auto lhs = dmc.newUInt32("bitwise_int_lhs");
	const auto rhs = dmc.newUInt32("bitwise_int_lhs");
	const auto converter = dmc.newXmm("bitwise_converter");
	dmc.movd(converter, flhs.Value);
	dmc.cvtss2si(lhs, converter);
	dmc.movd(converter, frhs.Value); // Good use of SIMD right here
	dmc.cvtss2si(rhs, converter);
	switch(op)
	{
	case BINARY_AND:
		dmc.and_(lhs, rhs);
		break;
	case BINARY_OR:
		dmc.or_(lhs, rhs);
		break;
	case BINARY_XOR:
		dmc.xor_(lhs, rhs);
		break;
	default:
		Core::Alert("Unknown binary operation");
	}
	dmc.cvtsi2ss(converter, lhs);
	dmc.movd(result.Value, converter);
}

static void Emit_BitwiseNot(DMCompiler& dmc)
{
	const auto num = dmc.popStack();
	const auto result = dmc.pushStack(imm(NUMBER), imm(0));
	const auto converter = dmc.newXmm("bitwise_converter");
	dmc.movd(converter, num.Value);
	dmc.cvtss2si(result.Value, converter);
	dmc.not_(result.Value);
	dmc.cvtsi2ss(converter, result.Value);
	dmc.movd(result.Value, converter);
}

static void Emit_AssocList(DMCompiler& dmc, unsigned int num_entries)
{
	auto result = Variable();
	InvokeNode* create;
	dmc.invoke(&create, (uint64_t)create_list, FuncSignatureT<asmjit::Type::I64, Value*, unsigned int>());
	create->setArg(0, imm(0));
	create->setArg(1, imm(0));
	create->setRet(0, result.Type);
	create->setRet(1, result.Value);
	while(num_entries--)
	{
		const auto value = dmc.popStack();
		const auto key = dmc.popStack();
		InvokeNode* set_at;
		dmc.invoke(&set_at, (uint64_t)SetAssocElement, FuncSignatureT<void, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int>());
		create->setArg(0, result.Type);
		create->setArg(1, result.Value);
		create->setArg(2, key.Value);
		create->setArg(3, key.Value);
		create->setArg(4, value.Value);
		create->setArg(5, value.Value);
	}
	dmc.pushStackRaw(result);
}

static bool Emit_Block(DMCompiler& dmc, const ProcBlock& block, const std::map<unsigned int, ProcBlock>& blocks, const unsigned int proc_id)
{
	/*block.node = */
	dmc.addBlock(block.label, block.may_sleep);

	for (size_t i = 0; i < block.contents.size(); i++)
	{
		const Instruction& instr = block.contents[i];

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
				jit_out << "Assembling write to field" << std::endl;
				Emit_FieldWrite(dmc, instr);
				break;
			default:
				if (instr.bytes()[1] > 64000)
				{
					Core::Alert("Failed to assemble setvar");
				}
				else
				{
					jit_out << "Assembling set cached field" << std::endl;
					Emit_SetCachedField(dmc, instr.bytes()[1]);
				}
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
				jit_out << "Assembling field read" << std::endl;
				Emit_FieldRead(dmc, instr);
				break;
			case AccessModifier::WORLD:
				dmc.pushStack(imm(DataType::WORLD_D), imm(0));
				break;
			default:
				if (instr.bytes()[1] > 64000)
				{
					Core::Alert("Failed to assemble getvar");
				}
				else
				{
					jit_out << "Assembling get cached field" << std::endl;
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
		case Bytecode::OUTPUT:
			jit_out << "Assembling output" << std::endl;
			dmc.setInlineComment("output");
			Emit_Output(dmc);
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
		case Bytecode::CALL:
		case Bytecode::CALLNR:
			jit_out << "Assembling call" << std::endl;
			dmc.setInlineComment("call proc");
			Emit_Call(dmc, instr);
			break;
		case Bytecode::CALLPARENT:
			jit_out << "Assembling default call parent" << std::endl;
			dmc.setInlineComment("call default parent");
			Emit_CallParent(dmc, proc_id);
			break;
		case Bytecode::CREATELIST:
			jit_out << "Assembling create list" << std::endl;
			Emit_CreateList(dmc, instr.bytes()[1]);
			break;
		case Bytecode::ASSOC_LIST:
			jit_out << "Assembling create assoc list" << std::endl;
			Emit_AssocList(dmc, instr.bytes()[1]);
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
		case Bytecode::SLEEP:
			jit_out << "Assembling sleep" << std::endl;
			Emit_Sleep(dmc);
			break;
		case Bytecode::JMP:
		case Bytecode::JMP2:
			jit_out << "Assembling jump" << std::endl;
			Emit_Jump(dmc, instr.bytes()[1], blocks);
			break;
		case Bytecode::JUMP_FALSE:
		case Bytecode::JUMP_FALSE2:
		case Bytecode::JUMP_TRUE:
		case Bytecode::JUMP_TRUE2:
			jit_out << "Assembling conditional jump" << std::endl;
			Emit_ConditionalJump(dmc, (Bytecode)instr.bytes()[0], instr.bytes()[1], blocks);
			break;
		case Bytecode::TEQ:
		case Bytecode::TNE:
		case Bytecode::TL:
		case Bytecode::TLE:
		case Bytecode::TG:
		case Bytecode::TGE:
			jit_out << "Assembling comparison" << std::endl;
			Emit_Comparison(dmc, (Bytecode)instr.bytes()[0]);
			break;
		case Bytecode::JMP_AND:
		case Bytecode::JMP_OR:
			jit_out << "Assembling chain jump" << std::endl;
			Emit_ConditionalChainJump(dmc, (Bytecode)instr.bytes()[0], instr.bytes()[1], blocks);
			break;
		case Bytecode::TEST:
			jit_out << "Assembling test" << std::endl;
			Emit_Test(dmc);
			break;
		case Bytecode::GETFLAG:
			jit_out << "Assembling getflag" << std::endl;
			Emit_GetFlag(dmc);
			break;
		case Bytecode::NOT:
			jit_out << "Assembling logical not" << std::endl;
			Emit_LogicalNot(dmc);
			break;
		case Bytecode::BINARY_AND:
		case Bytecode::BINARY_OR:
		case Bytecode::BINARY_XOR:
			jit_out << "Assembling bitwise op" << std::endl;
			Emit_BitwiseOp(dmc, (Bytecode)instr.bytes()[0]);
			break;
		case Bytecode::BITWISE_NOT:
			jit_out << "Assembling bitwise not" << std::endl;
			Emit_BitwiseNot(dmc);
			break;
		case Bytecode::ISTYPE:
			jit_out << "Assembling istype" << std::endl;
			Emit_IsType(dmc);
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
			Core::Alert("Compilation failed!");
			return false;
			break;
		}
	}

	dmc.endBlock();
	return true;
}


static std::ofstream bytecode_out("bytecode.txt");
static asmjit::JitRuntime rt;

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

static bool compile(const std::vector<Core::Proc const*> procs, DMCompiler* parent_compiler = nullptr)
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

		auto opt_result = analyze_proc(*proc);

		if (!opt_result)
		{
			Core::Alert("Analysis for " + proc->name + " failed.");
			return false;
		}

		auto result = *opt_result;

		for (auto& [k, v] : result.blocks)
		{
			v.label = dmc.newLabel();
		}

		ProcNode* node = dmc.addProc(result.local_count, result.argument_count, result.needs_sleep);
		entrypoints.push_back(node->_entryPoint);
		for (auto& [k, v] : result.blocks)
		{
			if (!Emit_Block(dmc, v, result.blocks, proc->id))
			{
				fflush(fuck);
				return false;
			}
		}
		dmc.endProc();
	}

	dmc.finalize();
	void* code_base = nullptr;
	rt.add(&code_base, &code);

	for (size_t i = 0; i < procs.size(); i++)
	{
		void* entrypoint = reinterpret_cast<void*>(reinterpret_cast<uint32_t>(code_base) + code.labelOffset(entrypoints.at(i)));
		jitted_procs.emplace(procs.at(i)->id, JittedInfo(entrypoint, false));
		jit_out << procs.at(i)->name << ": " << entrypoint << std::endl;
	}
	fflush(fuck);
	return true;
}