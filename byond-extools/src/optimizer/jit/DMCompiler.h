#pragma once

#include "../../core/core.h"
#include "../../third_party/asmjit/asmjit.h"

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
	Operand Type;
	Operand Value;
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
	ProcNode* addProc(uint32_t locals_count);
	void endProc();

	BlockNode* addBlock(Label& label, uint32_t continuation_index = -1);
	void endBlock();

	Variable getLocal(uint32_t index);
	void setLocal(uint32_t index, Variable& variable);

	template<std::size_t I>
	std::array<Variable, I> popStack()
	{
		if (_currentBlock == nullptr)
			__debugbreak();
		BlockNode& block = *_currentBlock;

		// The stack cache could be empty if something was pushed to it before jumping to a new block
		if (!block._stack.empty())
		{
			std::array<Variable, I> res;
			for (int i = 1; i <= I; i++)
			{
				res[I - i] = block._stack.pop(); // Pop and place in correct order
			}
			_currentBlock->_stack_top_offset -= I;
			return res;
		}

		setInlineComment("popStack (overpopped)");

		auto stack_top = block._stack_top;

		std::array<Variable, I> result;
		for (int i = 1; i <= I; i++)
		{
			auto type = newUInt32();
			auto value = newUInt32();
			mov(type, x86::ptr(stack_top, (block._stack_top_offset - (i - 1)) * sizeof(Value) - sizeof(Value), sizeof(uint32_t)));
			mov(value, x86::ptr(stack_top, (block._stack_top_offset - (i - 1)) * sizeof(Value) - (sizeof(Value) / 2), sizeof(uint32_t)));
			result[I - i] = { type, value };
		}

		_currentBlock->_stack_top_offset -= I;
		return result;
	}

	Variable popStack()
	{
		return popStack<1>()[0];
	}


	void pushStack(Variable& variable);
	void clearStack();

	// Commits the temporary stack variables - this is called automatically when a block ends
	void commitStack();

	// Commits the local variables to memory - you have to call this before anything that might yield!
	void commitLocals();

	void jump_zero(BlockNode* block);

	void jump(BlockNode* block);

	void jump_ge(BlockNode* block);

	x86::Gp getStackFrame();

	x86::Gp getCurrentIterator();
	void setCurrentIterator(Operand iter);

	// Returns the value at the top of the stack
	void doReturn();

private:
	ProcNode* _currentProc;
	BlockNode* _currentBlock;
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

		_stack_top = dmc.newUIntPtr();
		cb->_newNodeT<BlockEndNode>(&_end);
		//_stack.init(&cb->_allocator);		
	}

	Label _label;
	x86::Gp _stack_top;
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
	ProcNode(BaseBuilder* cb, uint32_t locals_count)
		: BaseNode(cb, static_cast<uint32_t>(NodeTypes::kNodeProc), kFlagHasNoEffect)
		, _locals_count(locals_count)
		, _end(nullptr)
	{
		DMCompiler& dmc = *static_cast<DMCompiler*>(cb);

		_jit_context = dmc.newUIntPtr();
		_stack_frame = dmc.newUIntPtr();
		_current_iterator = dmc.newUIntPtr();
		_entryPoint = dmc.newLabel();
		_prolog = dmc.newLabel();
		dmc._newNodeT<ProcEndNode>(&_end);

		// Allocate space for all of our locals
		_locals = dmc._allocator.allocT<Local>(locals_count * sizeof(Local));

		// Init the locals
		Local default_local {Local::CacheState::Modified, {Imm(DataType::NULL_D), Imm(0)}};
		for (uint32_t i = 0; i < locals_count; i++)
		{
			_locals[i] = default_local;
		}

		_blocks.reset();
		_continuationPoints.reset();
	}

	x86::Gp _jit_context;
	x86::Gp _stack_frame;
	x86::Gp _current_iterator;

	Label _entryPoint;
	Label _prolog;

	ZoneVector<Label> _continuationPoints;

	Local* _locals;
	uint32_t _locals_count;

	// The very very end of our proc. Nothing of this proc exists after this node.
	ProcEndNode* _end;

	// its all our blocks (TODO: maybe not needed)
	ZoneVector<BlockNode*> _blocks;
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

