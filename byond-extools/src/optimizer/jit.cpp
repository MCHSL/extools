#include "../core/core.h"
#include "../dmdism/instruction.h"
#include "../dmdism/disassembly.h"
#include <set>
#include "../third_party/asmjit/asmjit.h"
#include <fstream>
#include "jit.h"
#include <algorithm>

using namespace asmjit;

struct JitContext;

typedef bool (*JitProc)(JitContext* ctx);

struct JitContext
{
	JitContext()
	{
		dot = Value::Null();
		stack_top = &stack[0];
		stack_base = &stack[0];
		memset(&stack[0], 0, sizeof(stack));
	}

	JitContext(const JitContext&) = delete;
	JitContext& operator=(const JitContext&) = delete;

	// ProcConstants* what_called_us;

	Value dot;

	// Top of the stack. This is where the next stack entry to be pushed ill be located.
	Value* stack_top;

	// Base of the stack. Different to `&stack[0]` as this is the base of the currently executing proc's stack.
	Value* stack_base;

	Value stack[1024];
};

x86::Gp jit_context;
static unsigned int jit_co_suspend_proc_id = 0;

static size_t locals_count = 2;
Label cleanup_locals;

// exists to handle caching of register allocations for stack entries so we don't have to read from the real stack unless it might have changed
size_t last_commit_size;
std::vector<std::optional<std::pair<Operand, Operand>>> stack_cache;

static void SetStack(x86::Compiler& cc, int index, Operand type, Operand value)
{
	if (index >= stack_cache.size())
	{
		__debugbreak();
	}

	cc.setInlineComment("SetStack");
	stack_cache[index] = std::make_pair(type, value);
}

// Just fetches the Nth entry of the current proc's stack.
static std::pair<Operand, Operand> GetStack(x86::Compiler& cc, int index)
{
	if (index >= stack_cache.size())
	{
		__debugbreak();
	}

	// Check the cache first
	std::optional<std::pair<Operand, Operand>> cached = stack_cache[index];
	if (cached.has_value())
	{
		return *cached;
	}

	// They weren't in the cache: read from JitContext
	cc.setInlineComment("GetStack");

	x86::Gp stack_index = cc.newIntPtr();
	cc.mov(stack_index, x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(JitContext::stack_top)));
	cc.sub(stack_index, sizeof(Value) * (stack_cache.size() - index));

	x86::Gp type = cc.newInt32();
	x86::Gp value = cc.newInt32();
	cc.mov(type, x86::ptr(stack_index, 0, sizeof(uint32_t)));
	cc.mov(value, x86::ptr(stack_index, 1 * sizeof(Value) / 2, sizeof(uint32_t)));
	return std::make_pair(type, value);
}

static void PushStack(x86::Compiler& cc, Operand type, Operand value)
{
	stack_cache.push_back(std::make_pair(type, value));
}

// Pops & returns the type/value operands of the value at the top of the stack.
static std::pair<Operand, Operand> PopStack(x86::Compiler& cc)
{
	// Check the cache first
	std::optional<std::pair<Operand, Operand>> cached = stack_cache.back();
	stack_cache.pop_back();
	if (cached.has_value())
	{
		return *cached;
	}

	// They weren't in the cache: read from JitContext
	cc.setInlineComment("PopStack");

	x86::Gp stack_index = cc.newIntPtr();
	cc.mov(stack_index, x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(JitContext::stack_top)));
	cc.sub(stack_index, sizeof(Value));

	x86::Gp type = cc.newInt32();
	x86::Gp value = cc.newInt32();
	cc.mov(type, x86::ptr(stack_index, 0, sizeof(uint32_t)));
	cc.mov(value, x86::ptr(stack_index, 1 * sizeof(Value) / 2, sizeof(uint32_t)));
	return std::make_pair(type, value);
}

// Clear our cached registers and put everything in our JitContext.
// This has to be done whenever a JIT proc yields or returns.
// TODO: We don't need to commit updated locals if we're not yielding
// TODO: Could keep Imm values cached (but still commit them) if we're not leaving scope?
static void CommitStack(x86::Compiler& cc)
{
	cc.setInlineComment("CommitStack");

	x86::Gp current_stack_top = cc.newIntPtr();
	cc.mov(current_stack_top, x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(JitContext::stack_top)));

	size_t stack_size = stack_cache.size();
	for (size_t i = 0; i < stack_size; i++)
	{
		std::optional<std::pair<Operand, Operand>>& cached = stack_cache[i];

		if (!cached.has_value())
			continue;

		Operand& type = cached->first;
		Operand& value = cached->second;

		if (type.isImm())
		{
			cc.mov(x86::ptr(current_stack_top, i * sizeof(Value), sizeof(uint32_t)), type.as<Imm>());
		}
		else
		{
			cc.mov(x86::ptr(current_stack_top, i * sizeof(Value), sizeof(uint32_t)), type.as<x86::Gp>());
		}

		if (value.isImm())
		{
			cc.mov(x86::ptr(current_stack_top, i * sizeof(Value) + offsetof(Value, value), sizeof(uint32_t)), value.as<Imm>());
		}
		else
		{
			cc.mov(x86::ptr(current_stack_top, i * sizeof(Value) + offsetof(Value, value), sizeof(uint32_t)), value.as<x86::Gp>());
		}

		cached.reset();
	}

	size_t new_stack_length = stack_size;
	if (new_stack_length > last_commit_size)
	{
		cc.add(x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(JitContext::stack_top)), Imm((new_stack_length - last_commit_size) * sizeof(Value)));
	}
	else if (new_stack_length < last_commit_size)
	{
		cc.sub(x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(JitContext::stack_top)), Imm((last_commit_size - new_stack_length) * sizeof(Value)));
	}

	last_commit_size = new_stack_length;
}

// Pushes all of our locals onto the stack. They'll stay there for the whole proc.
// Doesn't actually emit code...
static void EmitPrologue(x86::Compiler& cc)
{	
	for(size_t i = 0; i < locals_count; i++)
	{
		// Make sure to init our operands here to avoid having a cold cache
		PushStack(cc, Imm(DataType::NULL_D), Imm(0));
	}
}

// Pops all of our locals off the stack
static void EmitEpilogue(x86::Compiler& cc)
{	
	// Should have only the locals and a return value on the stack
	if (stack_cache.size() != locals_count + 1)
	{
		__debugbreak();
	}

	// TODO: We don't actually have to commit here - only the ret-value is needed on the stack for the following code
	// Could lead to much smaller code if changed
	CommitStack(cc);

	// Pop value from our cache - but not from the real stack as we're about to do that ourself
	stack_cache.pop_back();
	
	cc.setInlineComment("EmitEpilogue");

	// The the ret value is on the top of the stack. Let's grab it first
	x86::Gp stack_ptr = cc.newIntPtr();
	cc.mov(stack_ptr, x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(JitContext::stack_top)));

	x86::Gp ret_type = cc.newInt32();
	x86::Gp ret_value = cc.newInt32();
	cc.mov(ret_type, x86::ptr(stack_ptr, -sizeof(Value), sizeof(uint32_t)));
	cc.mov(ret_value, x86::ptr(stack_ptr, -(sizeof(Value) / 2), sizeof(uint32_t)));

	// Now remove the ret and locals from the stack (minus 1)
	cc.sub(x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(JitContext::stack_top)), Imm(locals_count * sizeof(Value)));

	// Overwrite the last local with our ret value
	cc.mov(x86::ptr(stack_ptr, sizeof(Value) * -locals_count - sizeof(Value), sizeof(uint32_t)), ret_type.as<x86::Gp>());
	cc.mov(x86::ptr(stack_ptr, sizeof(Value) * -locals_count - sizeof(Value) / 2, sizeof(uint32_t)), ret_value.as<x86::Gp>());

	cc.ret();
}

// Shouldn't be globals!!!
static std::map<unsigned int, FuncNode*> proc_funcs;
static std::map<unsigned int, Label> proc_labels;
static std::map<unsigned int, Label> proc_epilogues;
static std::set<std::string> string_set;

static std::vector<Label> resumption_labels;
static std::vector<JitProc> resumption_procs;

static Label current_epilogue;

static uint32_t EncodeFloat(float f)
{
	return *reinterpret_cast<uint32_t*>(&f);
}

static float DecodeFloat(uint32_t i)
{
	return *reinterpret_cast<float*>(&i);
}

std::map<unsigned int, Block> split_into_blocks(Disassembly& dis, x86::Assembler& ass, size_t& locals_max_count)
{
	std::map<unsigned int, Block> blocks;
	unsigned int current_block_offset = 0;
	blocks[current_block_offset] = Block(0);
	std::set<unsigned int> jump_targets;
	for (Instruction& i : dis)
	{
		if((i == Bytecode::SETVAR || i == Bytecode::GETVAR) && i.bytes()[1] == AccessModifier::LOCAL)
		{
			uint32_t local_idx = i.bytes()[2];
			locals_max_count = std::max(locals_max_count, local_idx + 1);
		}

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
	static std::ofstream o("blocks.txt");
	o << "BEGIN: " << dis.proc->name << '\n';
	for (auto& [offset, block] : blocks)
	{
		block.label2 = ass.newLabel();
		o << offset << ":\n";
		for (const Instruction& i : block.contents)
		{
			o << i.opcode().tostring() << "\n";
		}
		o << "\n";
	}
	o << '\n';
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
x86::Gp src_type;
x86::Gp src_value;
x86::Gp arglist_ptr;
x86::Mem dot;

/*
void set_value(x86::Compiler& cc, x86::Mem& loc, Operand& type, Operand& value, unsigned int offset = 0)
{
	loc.setSize(4);
	loc.setOffset(offset * sizeof(trvh) + offsetof(trvh, type));
	cc.mov(loc, type.as<Imm>());
	loc.setOffset(offset * sizeof(trvh) + offsetof(trvh, value));
	cc.mov(loc, value.as<Imm>());
}
*/

/*
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
*/

void set_local(x86::Compiler& cc, unsigned int id)
{
	auto v = PopStack(cc);
	SetStack(cc, id, v.first, v.second);
}

void get_local(x86::Compiler& cc, unsigned int id)
{
	auto v = GetStack(cc, id);
	PushStack(cc, v.first, v.second);
}

/*
void set_arg_ptr(x86::Compiler& cc)
{
	arglist_ptr = cc.newInt32("arglist_ptr");
	cc.setArg(1, arglist_ptr);
}
*/

/*
void set_src(x86::Compiler& cc)
{
	src_type = cc.newInt32("src_type");
	src_value = cc.newInt32("src_value");
	cc.setArg(2, src_type);
	cc.setArg(3, src_value);
}
*/

/*
void get_src(x86::Compiler& cc)
{
	stack.push_back(src_type);
	stack.push_back(src_value);
}
*/

/*
void set_dot(x86::Compiler& cc)
{
	set_value(cc, dot, stack.at(stack.size() - 2), stack.at(stack.size() - 1));
	stack.erase(stack.end() - 2, stack.end());
}
*/

/*
void get_dot(x86::Compiler& cc)
{
	get_value(cc, dot);
}
*/

/*
void allocate_dot(x86::Compiler& cc)
{
	dot = cc.newStack(8, 4);
	stack.push_back(Imm(DataType::NULL_D));
	stack.push_back(Imm(0));
	set_dot(cc);
}
*/

/*
void get_arg(x86::Compiler& cc, unsigned int id)
{
	get_value(cc, x86::ptr(arglist_ptr), id);
}
*/

void push_integer(x86::Compiler& cc, float f)
{
	PushStack(cc, Imm(DataType::NUMBER), Imm(EncodeFloat(f)));
}

bool compare_values(trvh left, trvh right)
{
	return left.value == right.value && left.type == right.type;
}

/*
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
		auto call = cc.call((uint64_t)GetVariable, FuncSignatureT<asmjit::Type::I64, int, int, int>());
		call->setArg(0, type);
		call->setArg(1, value);
		call->setArg(2, Imm(name));
		call->setRet(0, type);
		call->setRet(1, value);
	}
	
	stack.push_back(type);
	stack.push_back(value);

}
*/

/*
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
*/

void ret(x86::Compiler& cc)
{
	if (stack_cache.size() != 1 + locals_count)
	{
		__debugbreak();
	}

	EmitEpilogue(cc);
}

/*
void jump_zero(x86::Compiler& cc, Block& destination)
{
	x86::Gp cond = stack.at(stack.size() - 1).as<x86::Gp>();
	stack.erase(stack.end() - 1, stack.end());
	cc.test(cond, cond);
	cc.je(destination.label);
}
*/

void end(x86::Compiler& cc)
{
	if (stack_cache.size() != locals_count)
	{
		__debugbreak();
	}

	PushStack(cc, Imm(DataType::NULL_D), Imm(0));
	EmitEpilogue(cc);
}

/*
void jump(x86::Compiler& cc, Block& dest)
{
	cc.jmp(dest.label);
}
*/

void math_op(x86::Compiler& cc, Bytecode op)
{
	auto right_pair = PopStack(cc);
	auto left_pair = PopStack(cc);

	x86::Xmm fleft = cc.newXmm();
	x86::Xmm fright = cc.newXmm();
	if (left_pair.second.isImm())
	{
		x86::Mem l = cc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(left_pair.second.as<Imm>().value()));
		cc.movss(fleft, l);
	}
	else
	{
		cc.movd(fleft, left_pair.second.as<x86::Gp>());
	}

	if (right_pair.second.isImm())
	{
		x86::Mem r = cc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(right_pair.second.as<Imm>().value()));
		cc.movss(fright, r);
	}
	else
	{
		cc.movd(fright, right_pair.second.as<x86::Gp>());
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

	x86::Gp result = cc.newInt32();
	cc.movd(result, fleft);
	PushStack(cc, Imm(DataType::NUMBER), result);
}

/*
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
*/

void call_global(x86::Compiler& cc, unsigned int arg_count, unsigned int proc_id)
{
/*
	auto proc_func = proc_funcs.find(proc_id);
	if(proc_func != proc_funcs.end())
	{
		call_compiled_global(cc, arg_count, proc_func->second);
		return;
	}
*/

/*
	if (proc_id == jit_co_suspend_proc_id)
	{
		cc.setInlineComment("jit_co_suspend");
		
		return;
	}
*/


	cc.setInlineComment("CallGlobal");

	// TODO: Abstract this out to some friendly calls
	x86::Gp args_base = cc.newIntPtr();
	cc.mov(args_base, x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(JitContext::stack_top)));

	for (int i = 0; i < arg_count; i++)
	{
		auto arg_pair = PopStack(cc);

		if (arg_pair.first.isImm())
		{
			cc.mov(x86::ptr(args_base, i * sizeof(Value), sizeof(uint32_t)), arg_pair.first.as<Imm>());
		}
		else
		{
			cc.mov(x86::ptr(args_base, i * sizeof(Value), sizeof(uint32_t)), arg_pair.first.as<x86::Gp>());
		}

		if (arg_pair.second.isImm())
		{
			cc.mov(x86::ptr(args_base, i * sizeof(Value) + sizeof(uint32_t), sizeof(uint32_t)), arg_pair.second.as<Imm>());
		}
		else
		{
			cc.mov(x86::ptr(args_base, i * sizeof(Value) + sizeof(uint32_t), sizeof(uint32_t)), arg_pair.second.as<x86::Gp>());
		}
	}

	cc.add(x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(JitContext::stack_top)), Imm(arg_count * sizeof(Value)));

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
	call->setArg(7, args_base);
	call->setArg(8, Imm(arg_count));
	call->setArg(9, Imm(0));
	call->setArg(10, Imm(0));
	call->setRet(0, ret_type);
	call->setRet(1, ret_value);

	cc.sub(x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(JitContext::stack_top)), Imm(arg_count * sizeof(Value)));

	PushStack(cc, ret_type, ret_value);
}

/*
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
*/

/*
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
*/

void push_value(x86::Compiler& cc, DataType type, unsigned int value, unsigned int value2 = 0)
{
	if (type == DataType::NUMBER)
	{
		value = value << 16 | value2;
	}

	PushStack(cc, Imm(type), Imm(value));
}

/*
void set_subvar(x86::Compiler& cc, Instruction& instr)
{
	if (instr.acc_chain.size() == 1)
	{
		if (instr.acc_base.first == AccessModifier::LOCAL)
		{
			get_local(cc, instr.acc_base.second);
		}
		else if (instr.acc_base.first == AccessModifier::ARG)
		{
			get_arg(cc, instr.acc_base.second);
		}
		else if (instr.acc_base.first == AccessModifier::SRC)
		{
			get_src(cc);
		}
		else
		{
			Core::Alert("FUCK!");
		}
		x86::Gp vtype = cc.newInt32();
		x86::Gp vvalue = cc.newInt32();
		auto call = cc.call((uint64_t)SetVariable, FuncSignatureT<void, int, int, int, int, int>());
		call->setArg(0, stack.at(stack.size() - 2).as<x86::Gp>());
		call->setArg(1, stack.at(stack.size() - 1).as<x86::Gp>());
		call->setArg(2, Imm(instr.acc_chain.at(0)));
		call->setArg(3, stack.at(stack.size() - 4).as<x86::Gp>());
		call->setArg(4, stack.at(stack.size() - 3).as<x86::Gp>());
		stack.erase(stack.end() - 4, stack.end());
	}
}
*/

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
			/*
			else if (instr.bytes()[1] == AccessModifier::SUBVAR)
			{
				jit_out << "Assembling set subvar" << std::endl;
				set_subvar(cc, instr);
			}
			else if (instr.bytes()[1] == AccessModifier::DOT)
			{
				jit_out << "Assembling set dot" << std::endl;
				set_dot(cc);
			}
			*/
			break;
		case Bytecode::GETVAR:
			/*
			if (instr.bytes()[1] == AccessModifier::ARG)
			{
				jit_out << "Assembling get arg" << std::endl;
				get_arg(cc, instr.bytes()[2]);
			}
			else*/ if (instr.bytes()[1] == AccessModifier::LOCAL)
			{
				jit_out << "Assembling get local" << std::endl;
				get_local(cc, instr.bytes()[2]);
			}/*
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
			else if (instr.bytes()[1] == AccessModifier::DOT)
			{
				jit_out << "Assembling get dot" << std::endl;
				get_dot(cc);
			}
			else
			{
				jit_out << "Unknown access modifier in get_var" << std::endl;
			}
			*/
			break;
		/*
		case Bytecode::TEQ:
			jit_out << "Assembling test equal" << std::endl;
			test_equal(cc);
			i += 1; //skip	POP
			break;
		*/
		/*
		case Bytecode::JZ:
			jit_out << "Assembling jump zero" << std::endl;
			jump_zero(cc, blocks[instr.bytes()[1]]);
			break;
		*/
		/*
		case Bytecode::JMP:
		case Bytecode::JMP2:
			jit_out << "Assembling unconditional jump" << std::endl;
			jump(cc, blocks[instr.bytes()[1]]);
			break;
		*/
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
		/*
		case Bytecode::CALL:
			jit_out << "Assembling normal call" << std::endl;
			call(cc, instr);
			break;
		*/
		case Bytecode::POP:
			PopStack(cc); // TODO: doesn't actually need ret val - could be optimized
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

// obviously not final, just some global state to test jit pause/resume
// reentrancy will kill it, don't complain
static JitContext stored_context;
static JitContext* current_context = nullptr;

static trvh JitEntryPoint(void* code_base, unsigned int args_len, Value* args, Value src)
{
	// Would be lovely for this to not be so... static
	JitContext ctx;
	JitProc code = static_cast<JitProc>(code_base);

	current_context = &ctx;
	code(&ctx);
	current_context = nullptr;

	if (ctx.stack_top != &ctx.stack[1])
	{
		__debugbreak();
	}
	
	trvh ret;
	ret.type = ctx.stack[0].type;
	ret.value = ctx.stack[0].value;
	return ret;
}

static void jit_co_suspend_internal()
{
	memcpy(&stored_context, current_context, sizeof(stored_context));
	stored_context.stack_base = (current_context->stack_base - current_context->stack) + stored_context.stack;
	stored_context.stack_top = (current_context->stack_top - current_context->stack) + stored_context.stack;
}

static trvh jit_co_suspend(unsigned int argcount, Value* args, Value src)
{
	// This should never run, it's a compiler intrinsic
	__debugbreak();
	return Value::Null();
}

static trvh jit_co_resume(unsigned int argcount, Value* args, Value src)
{
	Value resume_data = *(stored_context.stack_top - 1);

	// Now we push the value to be returned by jit_co_suspend
	if (argcount > 0)
	{
		*(stored_context.stack_top - 1) = args[0];
	}
	else
	{
		*(stored_context.stack_top - 1) = Value::Null();
	}

	JitProc code = resumption_procs[resume_data.value];
	code(&stored_context);

	if (stored_context.stack_top != &stored_context.stack[1])
	{
		__debugbreak();
	}

	trvh ret;
	ret.type = stored_context.stack[0].type;
	ret.value = stored_context.stack[0].value;
	return ret;
}


static void Emit_PushInteger(x86::Assembler& ass, float not_an_integer)
{
	ass.mov(x86::ecx, x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)));
	ass.add(x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)), sizeof(Value));

	ass.mov(x86::ptr(x86::ecx, 0, sizeof(uint32_t)), Imm(DataType::NUMBER));
	ass.mov(x86::ptr(x86::ecx, sizeof(Value) / 2, sizeof(uint32_t)), Imm(EncodeFloat(not_an_integer)));
}

static void Emit_SetLocal(x86::Assembler& ass, unsigned int id)
{
	ass.sub(x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)), sizeof(Value));

	ass.mov(x86::ecx, x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)));
	ass.mov(x86::edx, x86::ptr(x86::eax, offsetof(JitContext, stack_base), sizeof(uint32_t)));

	ass.push(x86::ebx);

	ass.mov(x86::ebx, x86::ptr(x86::ecx, 0, sizeof(uint32_t)));
	ass.mov(x86::ptr(x86::edx, id * sizeof(Value), sizeof(uint32_t)), x86::ebx);

	ass.mov(x86::ebx, x86::ptr(x86::ecx, sizeof(Value) / 2, sizeof(uint32_t)));
	ass.mov(x86::ptr(x86::edx, id * sizeof(Value) + sizeof(Value) / 2, sizeof(uint32_t)), x86::ebx);

	ass.pop(x86::ebx);
}

static void Emit_GetLocal(x86::Assembler& ass, unsigned int id)
{
	ass.mov(x86::ecx, x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)));
	ass.mov(x86::edx, x86::ptr(x86::eax, offsetof(JitContext, stack_base), sizeof(uint32_t)));

	ass.add(x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)), sizeof(Value));

	ass.push(x86::ebx);

	ass.mov(x86::ebx, x86::ptr(x86::edx, id * sizeof(Value), sizeof(uint32_t)));
	ass.mov(x86::ptr(x86::ecx, 0, sizeof(uint32_t)), x86::ebx);

	ass.mov(x86::ebx, x86::ptr(x86::edx, id * sizeof(Value) + sizeof(Value) / 2, sizeof(uint32_t)));
	ass.mov(x86::ptr(x86::ecx, sizeof(Value) / 2, sizeof(uint32_t)), x86::ebx);
	
	ass.pop(x86::ebx);
}

static void Emit_Pop(x86::Assembler& ass)
{
	ass.sub(x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)), sizeof(Value));
}

static void Emit_PushValue(x86::Assembler& ass, DataType type, unsigned int value, unsigned int value2 = 0)
{
	if (type == DataType::NUMBER)
	{
		value = value << 16 | value2;
	}

	ass.mov(x86::ecx, x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)));
	ass.add(x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)), sizeof(Value));

	ass.mov(x86::ptr(x86::ecx, 0, sizeof(uint32_t)), Imm(type));
	ass.mov(x86::ptr(x86::ecx, sizeof(Value) / 2, sizeof(uint32_t)), Imm(value));
}

static void Emit_CallGlobal(x86::Assembler& ass, unsigned int arg_count, unsigned int proc_id)
{
	if (proc_id == jit_co_suspend_proc_id)
	{
		ass.setInlineComment("jit_co_suspend intrinsic");

		Label resume = ass.newLabel();

		resumption_labels.push_back(resume);
		uint32_t resumption_index = resumption_labels.size() - 1;

		ass.mov(x86::ecx, x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)));
		ass.add(x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)), sizeof(Value));

		// where the code should resume
		ass.mov(x86::ptr(x86::ecx, 0, sizeof(uint32_t)), Imm(DataType::NULL_D));
		ass.mov(x86::ptr(x86::ecx, sizeof(Value) / 2, sizeof(uint32_t)), resumption_index);

		ass.push(x86::eax);
		ass.call((uint32_t) jit_co_suspend_internal);
		ass.pop(x86::eax);
		ass.jmp(current_epilogue);

		std::string comment = "Resumption Label: " + resumption_index;
		auto it = string_set.insert(comment);
		ass.setInlineComment(it.first->c_str());

		ass.bind(resume);
		// JitContext ptr lives in eax
		ass.mov(x86::eax, x86::ptr(x86::esp, 4, sizeof(uint32_t)));
		return;
	}


	ass.sub(x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)), arg_count * sizeof(Value));
	ass.mov(x86::ecx, x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)));

	ass.sub(x86::esp, 4 * 11 + 4 * 2);

	ass.mov(x86::ptr(x86::esp, 4 * 0, sizeof(uint32_t)), 0); // usr_type
	ass.mov(x86::ptr(x86::esp, 4 * 1, sizeof(uint32_t)), 0); // usr_value
	ass.mov(x86::ptr(x86::esp, 4 * 2, sizeof(uint32_t)), 2); // proc_type
	ass.mov(x86::ptr(x86::esp, 4 * 3, sizeof(uint32_t)), proc_id); // proc_id
	ass.mov(x86::ptr(x86::esp, 4 * 4, sizeof(uint32_t)), 0); // const_0
	ass.mov(x86::ptr(x86::esp, 4 * 5, sizeof(uint32_t)), 0); // src_type
	ass.mov(x86::ptr(x86::esp, 4 * 6, sizeof(uint32_t)), 0); // src_value
	ass.mov(x86::ptr(x86::esp, 4 * 7, sizeof(uint32_t)), x86::ecx); // argList
	ass.mov(x86::ptr(x86::esp, 4 * 8, sizeof(uint32_t)), arg_count); // argListLen
	ass.mov(x86::ptr(x86::esp, 4 * 9, sizeof(uint32_t)), 0); // const_0_2
	ass.mov(x86::ptr(x86::esp, 4 * 10, sizeof(uint32_t)), 0); // const_0_3

	ass.mov(x86::ptr(x86::esp, 4 * 11, sizeof(uint32_t)), x86::eax);
	ass.mov(x86::ptr(x86::esp, 4 * 12, sizeof(uint32_t)), x86::ecx);

	ass.call((uint32_t) CallGlobalProc);

	ass.mov(x86::ecx, x86::ptr(x86::esp, 4 * 12, sizeof(uint32_t)));

	ass.mov(x86::ptr(x86::ecx, 0, sizeof(uint32_t)), x86::eax);
	ass.mov(x86::ptr(x86::ecx, sizeof(Value) / 2, sizeof(uint32_t)), x86::edx);

	ass.mov(x86::eax, x86::ptr(x86::esp, 4 * 11, sizeof(uint32_t)));
	
	ass.add(x86::esp, 4 * 11 + 4 * 2);

	// Could merge into the sub above
	ass.add(x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)), sizeof(Value));
}

static void Emit_Return(x86::Assembler& ass)
{
	ass.jmp(current_epilogue);
}

static bool EmitBlock(x86::Assembler& ass, Block& block)
{
	ass.bind(block.label2);
	for (size_t i=0; i < block.contents.size(); i++)
	{
		Instruction& instr = block.contents[i];

		auto it = string_set.insert(instr.opcode().tostring());
		ass.setInlineComment(it.first->c_str());

		switch (instr.bytes()[0])
		{
		case Bytecode::PUSHI:
			jit_out << "Assembling push integer" << std::endl;
			Emit_PushInteger(ass, instr.bytes()[1]);
			break;
		case Bytecode::SETVAR:
			if (instr.bytes()[1] == AccessModifier::LOCAL)
			{
				jit_out << "Assembling set local" << std::endl;
				Emit_SetLocal(ass, instr.bytes()[2]);
			}
			break;
		case Bytecode::GETVAR:
			if (instr.bytes()[1] == AccessModifier::LOCAL)
			{
				jit_out << "Assembling get local" << std::endl;
				Emit_GetLocal(ass, instr.bytes()[2]);
			}
			break;
		case Bytecode::POP:
			jit_out << "Assembling pop" << std::endl;
			Emit_Pop(ass);
			break;
		case Bytecode::PUSHVAL:
			jit_out << "Assembling push value" << std::endl;
			if (instr.bytes()[1] == DataType::NUMBER) //numbers take up two DWORDs instead of one
			{
				Emit_PushValue(ass, (DataType)instr.bytes()[1], instr.bytes()[2], instr.bytes()[3]);
			}
			else
			{
				Emit_PushValue(ass, (DataType)instr.bytes()[1], instr.bytes()[2]);
			}
			break;
		case Bytecode::CALLGLOB:
			jit_out << "Assembling call global" << std::endl;
			Emit_CallGlobal(ass, instr.bytes()[1], instr.bytes()[2]);
			break;
		case Bytecode::RET:
			Emit_Return(ass);
			break;
		case Bytecode::DBG_FILE:
		case Bytecode::DBG_LINENO:
			break;
		default:
			jit_out << "Unknown instruction: " << instr.opcode().tostring() << std::endl;
			break;
		}
	}

	return true;
}

static void EmitPrologue(x86::Assembler& ass)
{
	// JitContext ptr lives in eax
	ass.mov(x86::eax, x86::ptr(x86::esp, 4, sizeof(uint32_t)));

	// Allocate locals in stack
	ass.mov(x86::ecx, x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)));
	ass.add(x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)), locals_count * sizeof(Value));

	// (null the locals too)
	for (size_t i = 0; i < locals_count; i++)
	{
		ass.mov(x86::ptr(x86::ecx, i * sizeof(Value), sizeof(uint32_t)), Imm(0));
		ass.mov(x86::ptr(x86::ecx, i * sizeof(Value) + sizeof(Value) / 2, sizeof(uint32_t)), Imm(0));
	}
}

static void EmitEpilogue(x86::Assembler& ass)
{
	ass.mov(x86::ecx, x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)));

	// Remove stack entries for our return value and all locals (except 1)
	ass.sub(x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)), locals_count * sizeof(Value));

	// Move our return value from its old stack position to its new one
	{
		// Type
		ass.mov(x86::edx, x86::ptr(x86::ecx, -sizeof(Value), sizeof(uint32_t)));
		ass.mov(x86::ptr(x86::ecx, sizeof(Value) * -locals_count - sizeof(Value), sizeof(uint32_t)), x86::edx);
	}
	{
		// Value
		ass.mov(x86::edx, x86::ptr(x86::ecx, -(sizeof(Value) / 2), sizeof(uint32_t)));
		ass.mov(x86::ptr(x86::ecx, sizeof(Value) * -locals_count - (sizeof(Value) / 2), sizeof(uint32_t)), x86::edx);
	}

	ass.ret();
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
	x86::Assembler ass(&code);
	std::ofstream asd("raw.txt");

	resumption_labels.clear();
	resumption_procs.clear();

	for (auto& proc : procs)
	{
		proc_labels[proc->id] = ass.newLabel();
		proc_epilogues[proc->id] = ass.newLabel();
	}

	for (auto& proc : procs)
	{
		Disassembly dis = proc->disassemble();
		asd << "BEGIN " << proc->name << '\n';
		for (Instruction& i : dis)
		{
			asd << i.bytes_str() << std::endl;
		}
		asd << "END " << proc->name << '\n';

		current_epilogue = proc_epilogues[proc->id];

		// needed?
		// last_commit_size = 0;
		// stack_cache.clear();

		// TODO: locals_count here is ass
		auto blocks = split_into_blocks(dis, ass, locals_count);

		// Prologue
		std::string comment = "Proc Prologue: " + proc->raw_path;
		auto it = string_set.insert(comment);
		ass.setInlineComment(it.first->c_str());
		ass.bind(proc_labels[proc->id]);
		EmitPrologue(ass);

		for (auto& [k, v] : blocks)
		{
			std::string comment = "Proc Block: " + proc->raw_path + "+" + std::to_string(v.offset);
			auto it = string_set.insert(comment);
			ass.setInlineComment(it.first->c_str());
			//ass.bind(proc_labels[proc->id]);
			EmitBlock(ass, v);
		}

		// Epilogue
		std::string comment2 = "Proc Epilogue: " + proc->raw_path;
		auto it2 = string_set.insert(comment2);
		ass.setInlineComment(it2.first->c_str());
		ass.bind(proc_epilogues[proc->id]);
		EmitEpilogue(ass);
	}

	jit_out << "Finalizing\n";
	int err = ass.finalize();
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

	// Setup resumption procs
	resumption_procs.resize(resumption_labels.size());
	for (size_t i = 0; i < resumption_labels.size(); i++)
	{
		resumption_procs[i] = reinterpret_cast<JitProc>(code_base + code.labelOffset(resumption_labels[i]));
	}

	// Hook procs
	for (auto& proc : procs)
	{
		Label& entry = proc_labels[proc->id];
		char* func_base = reinterpret_cast<char*>(code_base + code.labelOffset(entry));
		jit_out << func_base << std::endl;
		proc->jit_hook(func_base, JitEntryPoint);
	}

	jit_out << "Compilation successful" << std::endl;	
}

extern "C" EXPORT const char* jit_initialize(int n_args, const char** args)
{
	if (!Core::initialize())
	{
		return Core::FAIL;
	}
	
	Core::get_proc("/proc/jit_co_suspend").hook(jit_co_suspend);
	Core::get_proc("/proc/jit_co_resume").hook(jit_co_resume);

	jit_co_suspend_proc_id = Core::get_proc("/proc/jit_co_suspend").id;

	jit_compile({&Core::get_proc("/proc/jit_test_compiled_proc")});
	return Core::SUCCESS;
}
