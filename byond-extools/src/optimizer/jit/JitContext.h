#include "../../core/core.h"

namespace dmjit
{

class JitContext;

enum struct ProcResult : uint32_t
{
	// The proc finished. The return value is at the top of the stack (and should be the only value there)
	Success,

	// The proc is sleeping or something - the passed in JitContext is now invalid
	Yielded,
};

typedef ProcResult (*Proc)(JitContext* ctx, uint32_t continuation_index, unsigned int n_args, Value* args, trvh src, trvh usr);

struct DMListIterator
{
	DMListIterator* previous;
	Value* elements;
	std::uint32_t length;
	std::uint32_t current_index;
};

// This is pushed on the stack at the beginning of every proc call
struct ProcStackFrame
{
	// The ProcFrame of the function that called us. We need to restore this when we return.
	// If this is null it means we were directly called from DM and need to ret to our entrypoint
	ProcStackFrame* previous;
	uint32_t padding;
	// The stack of iterators implemented as a linked list. Points to the iterator currently being iterated.
	DMListIterator* current_iterator;
	uint32_t padding2;

	// TODO: This is where these will live
	Value src;
	Value usr;
	Value dot;

	// These variable-length arrays follow in memory, but are not strictly part of this struct
	// Value[] args;
	// Value[] locals;
	// Value[] temp_stack;
};

// Since we push ProcFrame to the stack we have to be sure it looks like a set of values to everybody else
static_assert(sizeof(ProcStackFrame) % sizeof(Value) == 0);


struct JitContext
{
	static const size_t DefaultSlotAllocation = 16;

	JitContext()
		: stack(new Value[DefaultSlotAllocation])
		, stack_allocated(DefaultSlotAllocation)
		, stack_frame(nullptr)
	{
		stack_top = stack;
	}

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
		stack_frame = reinterpret_cast<ProcStackFrame*>(&stack[reinterpret_cast<Value*>(src.stack_frame)- src.stack]);
		std::copy(src.stack, src.stack_top, stack);
	}

	JitContext& operator=(const JitContext& src)
	{
		delete[] stack;
		stack = new Value[stack_allocated];
		stack_top = &stack[src.stack_top - src.stack];
		stack_frame = reinterpret_cast<ProcStackFrame*>(&stack[reinterpret_cast<Value*>(src.stack_frame) - src.stack]);
		stack_allocated = src.stack_allocated;
		std::copy(src.stack, src.stack_top, stack);
	}

	JitContext(JitContext&& src) noexcept
		: stack(std::exchange(src.stack, nullptr))
		, stack_allocated(std::exchange(src.stack_allocated, 0))
		, stack_top(std::exchange(src.stack_top, nullptr))
		, stack_frame(std::exchange(src.stack_frame, nullptr))
	{}

	JitContext& operator=(JitContext&& src) noexcept
	{
		std::swap(stack, src.stack);
		std::swap(stack_allocated, src.stack_allocated);
		std::swap(stack_top, src.stack_top);
		std::swap(stack_frame, src.stack_frame);
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
		ProcStackFrame* old_stack_frame = stack_frame;

		stack_allocated += slots; // TODO: Growth factor?
		stack = new Value[stack_allocated];
		stack_top = &stack[old_stack_top - old_stack];
		stack_frame = reinterpret_cast<ProcStackFrame*>(&stack[reinterpret_cast<Value*>(old_stack_frame) - old_stack]);
		std::copy(old_stack, old_stack_top, stack);

		delete[] old_stack;
	}

	// Top of the stack. This is where the next stack entry to be pushed will be located.
	Value* stack_top;

	// Pointer into the stack of the current proc's frame
	ProcStackFrame* stack_frame;

	Value* stack;
	size_t stack_allocated;

	// Maybe?
	// ProcConstants* what_called_us;
	// Value dot;
};

}