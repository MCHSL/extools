#include "DMCompiler.h"
#include "JitContext.h"

#include <algorithm>
#include <array>

using namespace asmjit;

namespace dmjit
{

DMCompiler::DMCompiler(asmjit::CodeHolder& holder)
	: asmjit::x86::Compiler(&holder)
	, _currentProc(nullptr)
	, _currentBlock(nullptr)
{

}

ProcNode* DMCompiler::addProc(uint32_t locals_count, uint32_t args_count)
{
	if (_currentProc != nullptr)
		__debugbreak();
	addFunc(FuncSignatureT<uint32_t, JitContext*, uint32_t, uint32_t, Value*, uint32_t, uint32_t, uint32_t, uint32_t>(CallConv::kIdCDecl));
	_newNodeT<ProcNode>(&_currentProc, locals_count, args_count);
	addNode(_currentProc);

	setInlineComment("Proc Entrypoint");
	setArg(0, _currentProc->_jit_context);
	mov(_currentProc->_stack_frame, x86::ptr(_currentProc->_jit_context, offsetof(JitContext, stack_frame), sizeof(uint32_t)));

	x86::Gp continuation_index = newUInt32();
	setArg(1, continuation_index);
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

	static_assert(sizeof(ProcStackFrame) == sizeof(Value) * 5);
	mov(x86::ptr(stack_top, offsetof(Value, type), sizeof(uint32_t)), old_stack_frame);
	mov(x86::ptr(stack_top, offsetof(Value, value), sizeof(uint32_t)), old_stack_frame);
	add(x86::ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)), sizeof(ProcStackFrame));

	x86::Gp args_ptr = newUInt32();
	x86::Gp arg_count = newUInt32();
	setArg(2, arg_count);
	setArg(3, args_ptr);
	//todo: have the loop read the actual arg count and not the procnode one
	for (uint32_t i = 0; i < args_count; i++) // Copy args to stack frame
	{
		setInlineComment("mov arg");
		x86::Gp type = newUInt32();
		x86::Gp value = newUInt32();
		mov(type, x86::ptr(args_ptr, i * sizeof(Value) + offsetof(Value, type)));
		mov(value, x86::ptr(args_ptr, i * sizeof(Value) + offsetof(Value, value)));
		_currentProc->_args[i] = { Local::CacheState::Modified, Variable{type, value} };
	}

	// Set src type and value
	x86::Gp copy_thingy = newUInt32();
	x86::Gp copy_thingy2 = newUInt32();
	setArg(4, copy_thingy);
	mov(x86::ptr(_currentProc->_stack_frame, offsetof(ProcStackFrame, src) + offsetof(Value, type)), copy_thingy);
	setArg(5, copy_thingy2);
	mov(x86::ptr(_currentProc->_stack_frame, offsetof(ProcStackFrame, src) + offsetof(Value, value)), copy_thingy2);

	// Set the current iterator to nullptr
	mov(x86::ptr(_currentProc->_stack_frame, offsetof(ProcStackFrame, current_iterator), sizeof(uint32_t)), Imm(0));

	// Default locals to null
	// Casting Imms to Gp is kinda gross but it works
	Variable null{Imm(DataType::NULL_D).as<x86::Gp>(), Imm(0).as<x86::Gp>()};
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
	//_currentProc->_blocks.append(&_allocator, _currentBlock);
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
		return local.Variable;
	case Local::CacheState::Modified:
	case Local::CacheState::Stale:
		break;
	default:
		break;
	}

	auto stack_frame = proc._stack_frame;

	// Locals live after our stack frame
	auto type = newUInt32();
	auto value = newUInt32();
	mov(type, x86::ptr(stack_frame, sizeof(ProcStackFrame) + sizeof(Value) * proc._args_count + sizeof(Value) * index, sizeof(uint32_t))); // Locals are located after args.
	mov(value, x86::ptr(stack_frame, sizeof(ProcStackFrame) + sizeof(Value) * proc._args_count + sizeof(Value) * index + offsetof(Value, value), sizeof(uint32_t)));

	// Update the cache
	local = {Local::CacheState::Ok, {type, value}};

	return local.Variable;
}

// This and the above method have a lot in common, should be refactored.
Variable DMCompiler::getArg(uint32_t index)
{
	if (_currentProc == nullptr)
		__debugbreak();
	ProcNode& proc = *_currentProc;
	if (index >= proc._args_count)
		__debugbreak();

	Local& arg = proc._args[index];

	switch (arg.State)
	{
	case Local::CacheState::Ok:
	case Local::CacheState::Modified:
		return arg.Variable;
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
	mov(value, x86::ptr(stack_frame, sizeof(ProcStackFrame) + sizeof(Value) * index + offsetof(Value, value), sizeof(uint32_t)));

	// Update the cache
	arg = { Local::CacheState::Ok, {type, value} };

	return arg.Variable;
}

void DMCompiler::setLocal(uint32_t index, Variable& variable)
{
	if (_currentProc == nullptr)
		__debugbreak();
	ProcNode& proc = *_currentProc;
	if (index >= proc._locals_count)
		__debugbreak();
	proc._locals[index] = {Local::CacheState::Modified, variable};
	commitLocals();
}

Variable DMCompiler::getFrameEmbeddedValue(uint32_t offset)
{
	auto val = newUInt32();
	auto type = newUInt32();
	auto value = newUInt32();
	lea(val, x86::ptr(_currentProc->_stack_frame, offset));
	mov(type, x86::ptr(val, offsetof(Value, type)));
	mov(value, x86::ptr(val, offsetof(Value, value)));
	return { type, value };
}

Variable DMCompiler::getSrc()
{
	return getFrameEmbeddedValue(offsetof(ProcStackFrame, src));
}

Variable DMCompiler::getUsr()
{
	return getFrameEmbeddedValue(offsetof(ProcStackFrame, usr));
}

Variable DMCompiler::getDot()
{
	return getFrameEmbeddedValue(offsetof(ProcStackFrame, dot));
}

void DMCompiler::setDot(Variable& variable)
{
	auto dot = newUInt32();
	lea(dot, x86::ptr(_currentProc->_stack_frame, offsetof(ProcStackFrame, dot)));
	mov(x86::ptr(dot, offsetof(Value, type), sizeof(uint32_t)), variable.Type.as<x86::Gp>());
	mov(x86::ptr(dot, offsetof(Value, value), sizeof(uint32_t)), variable.Value.as<x86::Gp>());
}

void DMCompiler::pushStackRaw(Variable& variable)
{
	if (_currentBlock == nullptr)
		__debugbreak();
	BlockNode& block = *_currentBlock;
	_currentBlock->_stack_top_offset++;
	_currentBlock->_stack.append(&_allocator, variable);
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

Variable DMCompiler::pushStack()
{
	Variable var;
	var.Type = newInt32();
	var.Value = newInt32();
	pushStackRaw(var);
	return var;
}

Variable DMCompiler::pushStack(Operand type, Operand value)
{
	Variable var;
	var.Type = newInt32();
	var.Value = newInt32();
	pushStackRaw(var);
	mov(var.Type.as<x86::Gp>(), type.as<x86::Gp>());
	mov(var.Value.as<x86::Gp>(), value.as<x86::Gp>());
	return var;
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
				mov(x86::ptr(stack_frame, sizeof(ProcStackFrame) + sizeof(Value) * proc._args_count + i * sizeof(Value) + offsetof(Value, type), sizeof(uint32_t)), local.Variable.Type.as<Imm>());
			}
			else
			{
				mov(x86::ptr(stack_frame, sizeof(ProcStackFrame) + sizeof(Value) * proc._args_count +  i * sizeof(Value) + offsetof(Value, type), sizeof(uint32_t)), local.Variable.Type.as<x86::Gp>());
			}

			if (local.Variable.Value.isImm())
			{
				mov(x86::ptr(stack_frame, sizeof(ProcStackFrame) + sizeof(Value) * proc._args_count + i * sizeof(Value) + offsetof(Value, value), sizeof(uint32_t)), local.Variable.Value.as<Imm>());
			}
			else
			{
				mov(x86::ptr(stack_frame, sizeof(ProcStackFrame) + sizeof(Value) * proc._args_count + i * sizeof(Value) + offsetof(Value, value), sizeof(uint32_t)), local.Variable.Value.as<x86::Gp>());
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

void DMCompiler::jump_zero(Label label)
{
	if (_currentBlock == nullptr)
		__debugbreak();
	/*if (_currentProc->_blocks.indexOf(block) == Globals::kNotFound)
		__debugbreak();*/
	//Label label = newLabel();
	Variable var = popStack();

	// unnecessary
	commitStack();
	commitLocals();

	if (var.Value.isImm())
	{
		if (var.Value.as<Imm>().value() == 0)
		{
			
			jmp(label);
			return;
		}
	}
	else
	{
		test(var.Value.as<x86::Gp>(), var.Value.as<x86::Gp>());
		jz(label);
	}
}

void DMCompiler::jump(Label label)
{
	if (_currentBlock == nullptr)
		__debugbreak();
	/*if (_currentProc->_blocks.indexOf(block) == Globals::kNotFound)
		__debugbreak();*/
	commitStack(); // might not be necessary
	commitLocals();
	jmp(label);
}

x86::Gp DMCompiler::getStackFrame()
{
	if(_currentProc == nullptr)
		__debugbreak();
	return _currentProc->_stack_frame;
}

x86::Gp DMCompiler::getCurrentIterator()
{
	if (_currentProc == nullptr)
		__debugbreak();
	return _currentProc->_current_iterator;
}

void DMCompiler::setCurrentIterator(Operand iter)
{
	if (_currentProc == nullptr)
		__debugbreak();
	mov(_currentProc->_current_iterator, iter.as<x86::Gp>());
}

void delete_iterator(DMListIterator* iter)
{
	if (!iter)
	{
		return;
	}
	delete[] iter->elements;
	delete iter;
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
	auto retval = popStack();
	
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

	// Destroy the current iterator if there is one.
	auto del_iter = call((uint32_t)delete_iterator, FuncSignatureT<void, DMListIterator*>());
	del_iter->setArg(0, getCurrentIterator());
	
	// return Procresult::Success
	x86::Gp retcode = newUInt32();
	mov(retcode, Imm(static_cast<uint32_t>(ProcResult::Success)));
	ret(retcode);
}

}