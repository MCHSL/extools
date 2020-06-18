#include "DMCompiler.h"
#include "JitContext.h"

#include <algorithm>

using namespace asmjit;

namespace dmjit
{

DMCompiler::DMCompiler(asmjit::CodeHolder& holder)
	: asmjit::x86::Compiler(&holder)
	, _currentProc(nullptr)
	, _currentBlock(nullptr)
{

}

ProcNode* DMCompiler::addProc(uint32_t locals_count)
{
	if (_currentProc != nullptr)
		__debugbreak();
	addFunc(FuncSignatureT<uint32_t, JitContext*, uint32_t>(CallConv::kIdCDecl));
	_newNodeT<ProcNode>(&_currentProc, locals_count);
	addNode(_currentProc);

	setInlineComment("Proc Entrypoint");
	setArg(0, _currentProc->_jit_context);
	mov(_currentProc->_stack_frame, x86::ptr(_currentProc->_jit_context, offsetof(JitContext, stack_frame), sizeof(uint32_t)));

	x86::Gp continuation_index = newUInt32();
	// TODO: jump table

	setInlineComment("Proc Prolog");
	bind(_currentProc->_prolog);

	// New stack frame
	// TODO: allocate more space on stack if necessary
	x86::Gp stack_top = newUIntPtr();
	x86::Gp old_stack_frame = _currentProc->_stack_frame;
	_currentProc->_stack_frame = newUIntPtr();
	mov(stack_top, x86::ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)));
	mov(_currentProc->_stack_frame, stack_top);
	mov(old_stack_frame, x86::ptr(_currentProc->_jit_context, offsetof(JitContext, stack_frame), sizeof(uint32_t)));

	static_assert(sizeof(ProcStackFrame) == sizeof(Value) * 1);
	mov(x86::ptr(stack_top, offsetof(Value, type), sizeof(uint32_t)), old_stack_frame);
	mov(x86::ptr(stack_top, offsetof(Value, value), sizeof(uint32_t)), old_stack_frame);
	add(x86::ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)), sizeof(ProcStackFrame));

	// Default locals to null
	Variable null{Imm(DataType::NULL_D), Imm(0)};
	for (uint32_t i = 0; i < locals_count; i++)
	{
		setLocal(i, null);
	}
	
	// ..continues into first block
	return _currentProc;
}

void DMCompiler::endProc()
{
	if (_currentBlock != nullptr)
		__debugbreak();
	if (_currentProc == nullptr)
		__debugbreak();
	commitLocals();
	addNode(_currentProc->_end);
	endFunc();
	_currentProc = nullptr;
}

BlockNode* DMCompiler::addBlock(Label& label, uint32_t continuation_index)
{
	if (_currentProc == nullptr)
		__debugbreak();
	if (_currentBlock != nullptr)
		__debugbreak();

	_newNodeT<BlockNode>(&_currentBlock, label);
	_currentProc->_blocks.append(&_allocator, _currentBlock);
	setInlineComment("Block Start");
	bind(_currentBlock->_label);
	addNode(_currentBlock);
	mov(_currentBlock->_stack_top, x86::ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)));
	return _currentBlock;
}

void DMCompiler::endBlock()
{
	if (_currentBlock == nullptr)
		__debugbreak();
	commitStack();
	setInlineComment("Block End");
	addNode(_currentBlock->_end);
	_currentBlock = nullptr;
}

Variable DMCompiler::getLocal(uint32_t index)
{
	if (_currentProc == nullptr)
		__debugbreak();
	ProcNode& proc = *_currentProc;
	if (index >= proc._locals_count)
		__debugbreak();

	Local& local = proc._locals[index];

	switch (local.State)
	{
	case Local::CacheState::Ok:
	case Local::CacheState::Modified:
		return local.Variable;
	case Local::CacheState::Stale:
		break;
	default:
		break;
	}

	auto stack_frame = proc._stack_frame;

	// Locals live after our stack frame
	auto type = newUInt32();
	auto value = newUInt32();
	mov(type, x86::ptr(stack_frame, sizeof(ProcStackFrame) + sizeof(Value) * index, sizeof(uint32_t)));
	mov(value, x86::ptr(stack_frame, sizeof(ProcStackFrame) + sizeof(Value) * index + (sizeof(Value) / 2), sizeof(uint32_t)));

	// Update the cache
	local = {Local::CacheState::Ok, {type, value}};

	return local.Variable;
}

void DMCompiler::setLocal(uint32_t index, Variable& variable)
{
	if (_currentProc == nullptr)
		__debugbreak();
	ProcNode& proc = *_currentProc;
	if (index >= proc._locals_count)
		__debugbreak();
	proc._locals[index] = {Local::CacheState::Modified, variable};
}

Variable DMCompiler::popStack()
{
	if (_currentBlock == nullptr)
		__debugbreak();
	BlockNode& block = *_currentBlock;

	// The stack cache could be empty if something was pushed to it before jumping to a new block
	if (!block._stack.empty())
	{
		_currentBlock->_stack_top_offset--;
		return block._stack.pop();
	}

	setInlineComment("popStack (overpopped)");

	auto stack_top = block._stack_top;

	auto type = newUInt32();
	auto value = newUInt32();
	mov(type, x86::ptr(stack_top, block._stack_top_offset * sizeof(Value) - sizeof(Value), sizeof(uint32_t)));
	mov(value, x86::ptr(stack_top, block._stack_top_offset * sizeof(Value) - (sizeof(Value) / 2), sizeof(uint32_t)));

	_currentBlock->_stack_top_offset--;
	return {type, value};
}

void DMCompiler::pushStack(Variable& variable)
{
	if (_currentBlock == nullptr)
		__debugbreak();
	BlockNode& block = *_currentBlock;
	_currentBlock->_stack_top_offset++;
	_currentBlock->_stack.append(variable);
}

void DMCompiler::clearStack()
{
	if (_currentBlock == nullptr)
		__debugbreak();
	BlockNode& block = *_currentBlock;

	size_t i = 0;
	while(!block._stack.empty())
	{
		Variable var = block._stack.pop();
		i++;
	}

	_currentBlock->_stack_top_offset -= i;
}

void DMCompiler::commitStack()
{
	if (_currentBlock == nullptr)
		__debugbreak();
	BlockNode& block = *_currentBlock;

	setInlineComment("commitStack");

	x86::Gp stack_top = block._stack_top;

	size_t i = 0;
	while(!block._stack.empty())
	{
		Variable var = block._stack.pop();

		if (var.Type.isImm())
		{
			mov(x86::ptr(stack_top, -(i * sizeof(Value)) + offsetof(Value, type) + block._stack_top_offset * sizeof(Value), sizeof(uint32_t)), var.Type.as<Imm>());
		}
		else
		{
			mov(x86::ptr(stack_top,  -(i * sizeof(Value)) + offsetof(Value, type) + block._stack_top_offset * sizeof(Value), sizeof(uint32_t)), var.Type.as<x86::Gp>());
		}

		if (var.Value.isImm())
		{
			mov(x86::ptr(stack_top,  -(i * sizeof(Value)) + offsetof(Value, value) + block._stack_top_offset * sizeof(Value), sizeof(uint32_t)), var.Value.as<Imm>());
		}
		else
		{
			mov(x86::ptr(stack_top,  -(i * sizeof(Value)) + offsetof(Value, value) + block._stack_top_offset * sizeof(Value), sizeof(uint32_t)), var.Value.as<x86::Gp>());
		}

		i++;
	}

	if (block._stack_top_offset > 0)
	{
		add(x86::ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)), block._stack_top_offset * sizeof(Value));
	}
	else if (block._stack_top_offset < 0)
	{
		sub(x86::ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)), -(block._stack_top_offset * sizeof(Value)));
	}
}

void DMCompiler::commitLocals()
{
	if (_currentProc == nullptr)
		__debugbreak();
	ProcNode& proc = *_currentProc;

	setInlineComment("commitLocals");

	x86::Gp stack_frame = proc._stack_frame;

	for (uint32_t i = 0; i < proc._locals_count; i++)
	{
		Local& local = proc._locals[i];

		switch (local.State)
		{
		case Local::CacheState::Ok:
			break;
		case Local::CacheState::Modified:
			if (local.Variable.Type.isImm())
			{
				mov(x86::ptr(stack_frame, sizeof(ProcStackFrame) + i * sizeof(Value) + offsetof(Value, type), sizeof(uint32_t)), local.Variable.Type.as<Imm>());
			}
			else
			{
				mov(x86::ptr(stack_frame, sizeof(ProcStackFrame) + i * sizeof(Value) + offsetof(Value, type), sizeof(uint32_t)), local.Variable.Type.as<x86::Gp>());
			}

			if (local.Variable.Value.isImm())
			{
				mov(x86::ptr(stack_frame, sizeof(ProcStackFrame) + i * sizeof(Value) + offsetof(Value, value), sizeof(uint32_t)), local.Variable.Value.as<Imm>());
			}
			else
			{
				mov(x86::ptr(stack_frame, sizeof(ProcStackFrame) + i * sizeof(Value) + offsetof(Value, value), sizeof(uint32_t)), local.Variable.Value.as<x86::Gp>());
			}

			// i can't decide if this is valid
			//local.State = Local::CacheState::Ok;
			break;	
		case Local::CacheState::Stale:
			break;
		default:
			break;
		}
	}
}

void DMCompiler::jump_zero(BlockNode* block)
{
	if (_currentBlock == nullptr)
		__debugbreak();
	if (_currentProc->_blocks.indexOf(block) == Globals::kNotFound)
		__debugbreak();
	Label label = newLabel();
	Variable var = popStack();

	// unnecessary
	commitStack();

	if (var.Value.isImm())
	{
		if (var.Value.as<Imm>().value() == 0)
		{
			
			jmp(block->_label);
			return;
		}
	}
	else
	{
		test(var.Value.as<x86::Gp>(), var.Value.as<x86::Gp>());
		jz(block->_label);
	}
}

void DMCompiler::jump(BlockNode* block)
{
	if (_currentBlock == nullptr)
		__debugbreak();
	if (_currentProc->_blocks.indexOf(block) == Globals::kNotFound)
		__debugbreak();
	commitStack(); // might not be necessary
	jmp(block->_label);
}

void DMCompiler::doReturn()
{
	if (_currentProc == nullptr)
		__debugbreak();
	if (_currentBlock == nullptr)
		__debugbreak();

	ProcNode& proc = *_currentProc;
	BlockNode& block = *_currentBlock;

	// The only thing our proc should leave on the stack is the return value
	Variable retval = popStack();
	
	setInlineComment("doReturn");

	// Get the new stack_top value we'll be leaving behind
	x86::Gp stack_top = newUIntPtr();
	mov(stack_top, proc._stack_frame);

	// Restore previous stack frame
	x86::Gp prev_stack_frame = newUIntPtr();
	mov(prev_stack_frame, x86::ptr(proc._stack_frame, offsetof(ProcStackFrame, previous), sizeof(uint32_t)));
	mov(x86::ptr(proc._jit_context, offsetof(JitContext, stack_frame), sizeof(uint32_t)), prev_stack_frame);
		
	// Move return value to stack
	if (retval.Type.isImm())
	{
		mov(x86::ptr(stack_top, offsetof(Value, type), sizeof(uint32_t)), retval.Type.as<Imm>());
	}
	else
	{
		mov(x86::ptr(stack_top, offsetof(Value, type), sizeof(uint32_t)), retval.Type.as<x86::Gp>());
	}

	if (retval.Value.isImm())
	{
		mov(x86::ptr(stack_top, offsetof(Value, value), sizeof(uint32_t)), retval.Value.as<Imm>());
	}
	else
	{
		mov(x86::ptr(stack_top, offsetof(Value, value), sizeof(uint32_t)), retval.Value.as<x86::Gp>());
	}

	add(stack_top, sizeof(Value));
	mov(x86::ptr(proc._jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)), stack_top);
	
	// return Procresult::Success
	x86::Gp retcode = newUInt32();
	mov(retcode, Imm(static_cast<uint32_t>(ProcResult::Success)));
	ret(retcode);
}

}