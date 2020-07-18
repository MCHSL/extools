#pragma once

#include "../../core/core.h"
#include "../../third_party/asmjit/asmjit.h"
#include "JitContext.h"

#include <stdint.h>
#include <vector>
#include <array>

namespace dmjit
{
	using namespace asmjit;

class ProcNode;
class ProcEndNode;
class BlockNode;
class BlockEndNode;

enum class NodeTypes : uint32_t
{
	kNodeProc = BaseNode::kNodeUser,
	kNodeProcEnd,
	kNodeBlock,
	kNodeBlockEnd,
};

// Reference to a DM variable through operands
struct Variable
{
	// It says x86::Gp, but these can hold any type of asmjit operand.
	// asmjit keeps track of the type of the operand via an internal type ID,
	// so the type of variables here is irrelevant. We use x86::Gp to avoid casting to register everywhere.
	x86::Gp Type;
	x86::Gp Value;
};

// Reference to a local DM variable through operands
struct Local
{
	enum class CacheState
	{
		// We have operands representing the latest data in our cache
		Ok,

		// We haven't fetched this value from the JitContext stack yet
		Stale,

		// We've modified this in our cache but not committed it yet.
		Modified,
	};

	CacheState State;
	Variable Variable;
};

class DMCompiler
	: public x86::Compiler
{
public:
	DMCompiler(CodeHolder& holder);

public:
	ProcNode* addProc(uint32_t locals_count, uint32_t args_count, bool zzz);
	void endProc();

	BlockNode* addBlock(Label& label, uint32_t continuation_index = -1);
	void endBlock();

	Variable getLocal(uint32_t index);
	void setLocal(uint32_t index, const Variable& variable);

	Variable getArg(uint32_t index);

	Variable getFrameEmbeddedValue(uint32_t offset);

	x86::Gp getJitContext();

	Variable getSrc();
	Variable getUsr();

	Variable getDot();
	void setDot(const Variable& variable);

	Variable getCached();
	void setCached(const Variable& variable);

	template<std::size_t I>
	std::array<Variable, I> popStack()
	{
		if (_currentBlock == nullptr)
			__debugbreak();
		BlockNode& block = *_currentBlock;

		std::array<Variable, I> res;
		int popped_count = 0;

		// The stack cache could be empty if something was pushed to it before jumping to a new block
		for (; popped_count < I && !block._stack.empty(); popped_count++)
		{
			res[I - popped_count - 1] = block._stack.pop(); // Pop and place in correct order
		}
		_currentBlock->_stack_top_offset -= popped_count;

		if (popped_count == I)
		{
			return res;
		}

		const int diff = I - popped_count;

		setInlineComment("popStack (overpopped)");

		x86::Gp stack_top = newUIntPtr("stack_top");
		mov(stack_top, x86::dword_ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top)));
		//add(stack_top, block._stack_top_offset * sizeof(Value));

		for (; popped_count < I; popped_count++)
		{
			const auto type = newUInt32("pop_type");
			const auto value = newUInt32("pop_value");
			mov(type, x86::ptr(stack_top, (block._stack_top_offset - popped_count) * sizeof(Value) - sizeof(Value), sizeof(uint32_t)));
			mov(value, x86::ptr(stack_top, (block._stack_top_offset - popped_count) * sizeof(Value) - offsetof(Value, value), sizeof(uint32_t)));
			res[I - popped_count - 1] = { type, value };
		}
		if (popped_count == I)
		{
			_currentBlock->_stack_top_offset -= diff;
			return res;
		}

		Core::Alert("Failed to pop enough arguments from the stack");
		abort();
	}

	Variable popStack()
	{
		return popStack<1>()[0];
	}


	void pushStackRaw(const Variable& variable);
	void clearStack();

	// Calling these creates a new Variable on the stack and returns an instance of it.
	// You can then `mov` into variable.Type and Value to set the stack variable's fields appropriately.
	// When emitting conditional execution (cmp and jumps) remember that pushStack is a compile time thing
	// and executes whether or not the condition passes. This means that if a function is supposed to only push
	// one item onto the stack, you can only have one pushStack in it. Preferably put it near the top and write the result
	// to the Variable it returns.
	// This is important e.g. when you have an addition opcode, which takes different execution paths based
	// on the runtime type of arguments supplied.
	Variable pushStack();
	Variable pushStack(Operand type, Operand value);

	// Commits the temporary stack variables - this is called automatically when a block ends
	void commitStack();

	// Commits the local variables to memory - you have to call this before anything that might yield!
	void commitLocals();

	void jump_zero(Label label);

	void jump(Label label);

	x86::Gp getStackFramePtr();

	x86::Gp getCurrentIterator();
	void setCurrentIterator(Operand iter);

	// Returns the value at the top of the stack
	void doReturn(bool immediate = false);

	// Pauses execution until called again
	void doYield();

	// Pauses execution and asks the entry point to suspend for
	// the time specified in the Value on top of the stack.
	void doSleep();

	unsigned int addContinuationPoint();
	unsigned int prepareNextContinuationIndex();

private:
	ProcNode* _currentProc;
	BlockNode* _currentBlock;

	void _return(ProcResult code);
};


class BlockNode
	: public BaseNode
{
public:
	BlockNode(BaseBuilder* cb, Label& label)
		: BaseNode(cb, static_cast<uint32_t>(NodeTypes::kNodeBlock), kFlagHasNoEffect)
		, _label(label)
		, _stack_top_offset(0)
		, _end(nullptr)
	{
		DMCompiler& dmc = *static_cast<DMCompiler*>(cb);

		//_stack_top = dmc.newUIntPtr("block_stack_top");
		cb->_newNodeT<BlockEndNode>(&_end);
		//_stack.init(&cb->_allocator);		
	}

	Label _label;
	int32_t _stack_top_offset;
	ZoneVector<Variable> _stack;

	BlockEndNode* _end;	
};

class BlockEndNode
	: public BaseNode
{
public:
	BlockEndNode(BaseBuilder* cb)
		: BaseNode(cb, static_cast<uint32_t>(NodeTypes::kNodeBlockEnd), kFlagHasNoEffect)
	{}
};


// Represents an entire compiled proc
class ProcNode
	: public BaseNode
{
public:
	ProcNode(BaseBuilder* cb, uint32_t locals_count, uint32_t args_count, bool needs_sleep)
		: BaseNode(cb, static_cast<uint32_t>(NodeTypes::kNodeProc), kFlagHasNoEffect)
		, _locals_count(locals_count)
		, _args_count(args_count)
		, _end(nullptr)
		, _locals(nullptr)
		, needs_sleep(needs_sleep)
	{
		DMCompiler& dmc = *static_cast<DMCompiler*>(cb);

		_jit_context = dmc.newUIntPtr("_jit_context");
		_stack_frame = dmc.newUIntPtr("stack_frame");
		//_current_iterator = dmc.newUIntPtr("_current_iterator");
		_prolog = dmc.newLabel();
		_continuationPointTable = dmc.newLabel();
		_cont_points_annotation = dmc.newJumpAnnotation();
		dmc._newNodeT<ProcEndNode>(&_end);

		// Allocate space for all of our locals
		if (locals_count > 0)
		{
			_locals = dmc._allocator.allocT<Local>(locals_count * sizeof(Local));

			// Init the locals
			Local default_local{ Local::CacheState::Modified, {Imm(DataType::NULL_D).as<x86::Gp>(), Imm(0).as<x86::Gp>()} };
			for (uint32_t i = 0; i < locals_count; i++)
			{
				_locals[i] = default_local;
			}
		}


		// Do the same for arguments
		/*_args = dmc._allocator.allocT<Local>(args_count * sizeof(Local));
		for (uint32_t i = 0; i < args_count; i++)
		{
			_args[i] = default_local;
		}*/


		_continuationPoints.reset();
	}

	x86::Gp _jit_context;
	x86::Gp _stack_frame;
	//x86::Gp _current_iterator;

	bool needs_sleep;

	Label _entryPoint;
	Label _prolog;
	Label _continuationPointTable;

	ZoneVector<Label> _continuationPoints;

	Local* _locals;
	uint32_t _locals_count;

	//Local* _args;
	uint32_t _args_count;

	// The very very end of our proc. Nothing of this proc exists after this node.
	ProcEndNode* _end;

	// its all our blocks (TODO: maybe not needed)
	//ZoneVector<ProcBlock> _blocks;

	JumpAnnotation* _cont_points_annotation;
};

class ProcEndNode
	: public BaseNode
{
public:
	ProcEndNode(BaseBuilder* cb)
		: BaseNode(cb, static_cast<uint32_t>(NodeTypes::kNodeProcEnd), kFlagHasNoEffect)
	{}
};

}

