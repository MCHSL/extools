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

enum struct JitProcResult : uint32_t
{
	Success,
	
	// The proc is sleeping or something - the passed in JitContext is now invalid
	Yielded,
};

typedef JitProcResult (*JitProc)(JitContext* ctx);

struct JitContext
{
	static const size_t DefaultSlotAllocation = 16;

	JitContext()
		: stack(new Value[DefaultSlotAllocation])
		, stack_allocated(DefaultSlotAllocation)
		, stack_proc_base(nullptr)
		, stack_top(stack)
	{}

	~JitContext() noexcept
	{
		delete[] stack;
		stack = nullptr;
	}

	JitContext(const JitContext& src)
	{
		stack_allocated = src.stack_allocated;
		stack = new Value[stack_allocated];
		stack_top = &stack[src.stack_top - src.stack];
		stack_proc_base = &stack[src.stack_proc_base - src.stack];
		std::copy(src.stack, src.stack_top, stack);
	}

	JitContext& operator=(const JitContext& src)
	{
		delete[] stack;
		stack = new Value[stack_allocated];
		stack_top = &stack[src.stack_top - src.stack];
		stack_proc_base = &stack[src.stack_proc_base - src.stack];
		std::copy(src.stack, src.stack_top, stack);
	}

	JitContext(JitContext&& src) noexcept
		: stack(std::exchange(src.stack, nullptr))
		, stack_allocated(std::exchange(src.stack_allocated, 0))
		, stack_top(std::exchange(src.stack_top, nullptr))
		, stack_proc_base(std::exchange(src.stack_proc_base, nullptr))
	{}

	JitContext& operator=(JitContext&& src) noexcept
	{
		std::swap(stack, src.stack);
		std::swap(stack_allocated, src.stack_allocated);
		std::swap(stack_top, src.stack_top);
		std::swap(stack_proc_base, src.stack_proc_base);
		return *this;
	}

	// The number of slots in use
	size_t Count()
	{
		return stack_top - stack;
	}

	// Makes sure at least this many free slots are available
	// Called by JitProc prologues
	void Reserve(size_t slots)
	{
		size_t free_slots = stack_allocated - Count();

		if (free_slots >= slots)
			return;

		Value* old_stack = stack;
		Value* old_stack_top = stack_top;
		Value* old_stack_proc_base = stack_proc_base;

		stack_allocated += slots; // TODO: Growth factor?
		stack = new Value[stack_allocated];
		stack_top = &stack[old_stack_top - old_stack];
		stack_proc_base = &stack[old_stack_proc_base - old_stack];
		std::copy(old_stack, old_stack_top, stack);

		delete[] old_stack;
	}

	Value* stack;
	size_t stack_allocated;

	// Top of the stack. This is where the next stack entry to be pushed will be located.
	Value* stack_top;

	// Base of the current proc's stack
	Value* stack_proc_base;

	// Maybe?
	// ProcConstants* what_called_us;
	// Value dot;
};

x86::Gp jit_context;
static unsigned int jit_co_suspend_proc_id = 0;

// Shouldn't be globals!!!
static size_t locals_count = 0;
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

// obviously not final, just some global state to test jit pause/resume
// reentrancy will kill it, don't complain
static JitContext stored_context;
static JitContext* current_context = nullptr;

static trvh JitEntryPoint(void* code_base, unsigned int args_len, Value* args, Value src)
{
	JitContext ctx;
	JitProc code = static_cast<JitProc>(code_base);

	current_context = &ctx;
	JitProcResult res = code(&ctx);
	current_context = nullptr;

	switch (res)
	{
	case JitProcResult::Success:
		if (ctx.Count() != 1)
		{
			__debugbreak();
			return Value::Null();
		}

		return ctx.stack[0];
	case JitProcResult::Yielded:
		return Value::Null();
	}

	// Shouldn't be here
	__debugbreak();
	return Value::Null();
}

static void jit_co_suspend_internal()
{
	stored_context = std::move(*current_context);
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

	JitProcResult res = resumption_procs[resume_data.value](&stored_context);

	switch (res)
	{
		case JitProcResult::Success:
			if (stored_context.Count() != 1)
			{
				__debugbreak();
				return Value::Null();
			}

			return stored_context.stack[0];
		case JitProcResult::Yielded:
			return Value::Null();
	}
	
	// Shouldn't be here
	__debugbreak();
	return Value::Null();
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
	ass.mov(x86::edx, x86::ptr(x86::eax, offsetof(JitContext, stack_proc_base), sizeof(uint32_t)));

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
	ass.mov(x86::edx, x86::ptr(x86::eax, offsetof(JitContext, stack_proc_base), sizeof(uint32_t)));

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

		// Our JitContext* becomes invalid after this call
		ass.call((uint32_t) jit_co_suspend_internal);
		ass.mov(x86::eax, Imm(static_cast<uint32_t>(JitProcResult::Yielded)));
		ass.ret();

		std::string comment = "Resumption Label: " + std::to_string(resumption_index);
		auto it = string_set.insert(comment);
		ass.setInlineComment(it.first->c_str());

		// Emit a new entry point for our proc
		ass.bind(resume);
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

	// TODO: Call JitContext::Reserve()

	// TODO: We need to just define a struct of the stuff that goes before locals somewhere
	// Allocate room for our constant data. It's the previous stack_proc_base and our local vars
	ass.mov(x86::ecx, x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)));
	ass.add(x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)), (1 + locals_count) * sizeof(Value));

	// Store previous proc's stack_proc_base
	ass.mov(x86::edx, x86::ptr(x86::eax, offsetof(JitContext, stack_proc_base), sizeof(uint32_t)));
	ass.mov(x86::ptr(x86::ecx, 0, sizeof(uint32_t)), Imm(0));
	ass.mov(x86::ptr(x86::ecx, sizeof(Value) / 2, sizeof(uint32_t)), x86::edx);

	// Set new stack_proc_base
	ass.mov(x86::ptr(x86::eax, offsetof(JitContext, stack_proc_base), sizeof(uint32_t)), x86::ecx);

	// Set the locals to null
	for (size_t i = 0; i < locals_count; i++)
	{
		ass.mov(x86::ptr(x86::ecx, (1 + i) * sizeof(Value), sizeof(uint32_t)), Imm(0));
		ass.mov(x86::ptr(x86::ecx, (1 + i) * sizeof(Value) + sizeof(Value) / 2, sizeof(uint32_t)), Imm(0));
	}
}

static void EmitEpilogue(x86::Assembler& ass)
{
	ass.mov(x86::ecx, x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)));

	// Remove stack entries for our return value, the previous stack_proc_base, and all locals (except 1)
	ass.sub(x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)), (1 + locals_count) * sizeof(Value));

	// Read out the old stack_proc_base
	ass.mov(x86::edx, x86::ptr(x86::ecx, sizeof(Value) * -(1 + locals_count) - (sizeof(Value) / 2), sizeof(uint32_t)));
	ass.mov(x86::ptr(x86::eax, offsetof(JitContext, stack_proc_base), sizeof(uint32_t)), x86::edx);

	// Move our return value from its old stack position to its new one
	{
		// Type
		ass.mov(x86::edx, x86::ptr(x86::ecx, -sizeof(Value), sizeof(uint32_t)));
		ass.mov(x86::ptr(x86::ecx, sizeof(Value) * -(1 + locals_count) - sizeof(Value), sizeof(uint32_t)), x86::edx);
	}
	{
		// Value
		ass.mov(x86::edx, x86::ptr(x86::ecx, -(sizeof(Value) / 2), sizeof(uint32_t)));
		ass.mov(x86::ptr(x86::ecx, sizeof(Value) * -(1 + locals_count) - (sizeof(Value) / 2), sizeof(uint32_t)), x86::edx);
	}

	ass.mov(x86::eax, Imm(static_cast<uint32_t>(JitProcResult::Success)));
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
