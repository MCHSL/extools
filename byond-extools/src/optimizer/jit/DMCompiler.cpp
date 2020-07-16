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

ProcNode* DMCompiler::addProc(uint32_t locals_count, uint32_t args_count, bool zzz)
{
	if (_currentProc != nullptr)
		__debugbreak();

	auto label = newLabel();
	bind(label);
	addFunc(FuncSignatureT<uint32_t, JitContext*, uint32_t, Value*, uint32_t, uint32_t, uint32_t, uint32_t>(CallConv::kIdCDecl));

	_newNodeT<ProcNode>(&_currentProc, locals_count, args_count, zzz);
	addNode(_currentProc);

	_currentProc->_entryPoint = label;

	setInlineComment("Proc Entrypoint");
	setArg(0, _currentProc->_jit_context);
	x86::Gp stack_frame_ptr = getStackFramePtr();

	if (_currentProc->needs_sleep)
	{
		test(stack_frame_ptr, stack_frame_ptr);
		je(_currentProc->_prolog); // If there is no stack frame, create it.

		// If there is, we've been called before, so we need to jump to the given continuation point.
		x86::Gp continuation_index = newUInt32("cont_index");
		setInlineComment("Get Continuation Index");
		mov(continuation_index, x86::ptr(getStackFramePtr(), offsetof(ProcStackFrame, continuation_index), sizeof(uint32_t)));

		setInlineComment("Continuation Jump");
		x86::Gp cont_target = newUIntPtr("cont_target");
		x86::Gp cont_offset = newUIntPtr("cont_offset");
		lea(cont_offset, x86::ptr(_currentProc->_continuationPointTable));
		mov(cont_target, x86::ptr(cont_offset, continuation_index, 2));
		add(cont_target, cont_offset);
		jmp(cont_target, _currentProc->_cont_points_annotation);
	}


	setInlineComment("Proc Prolog");
	bind(_currentProc->_prolog);

	if (_currentProc->needs_sleep)
	{
		_currentProc->_cont_points_annotation->addLabel(_currentProc->_prolog);
		_currentProc->_continuationPoints.append(&_allocator, _currentProc->_prolog);
	}


	// New stack frame
	// TODO: allocate more space on stack if necessary

	x86::Gp previous = getStackFramePtr();
	x86::Gp stack_top = newUIntPtr("stack_top");
	mov(stack_top, x86::dword_ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top)));
	mov(x86::dword_ptr(_currentProc->_jit_context, offsetof(JitContext, stack_frame)), stack_top);
	x86::Gp new_frame = getStackFramePtr();
	mov(x86::dword_ptr(new_frame, offsetof(ProcStackFrame, previous)), previous);

	static_assert(sizeof(ProcStackFrame) == sizeof(Value) * 5);
	//mov(x86::ptr(stack_top, offsetof(Value, type), sizeof(uint32_t)), old_stack_frame);
	//mov(x86::ptr(stack_top, offsetof(Value, value), sizeof(uint32_t)), old_stack_frame);
	add(x86::dword_ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top)), sizeof(ProcStackFrame));
	add(x86::dword_ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top)), args_count * sizeof(Value));
	add(x86::dword_ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top)), locals_count * sizeof(Value));
	mov(stack_top, x86::dword_ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top)));
	mov(x86::dword_ptr(stack_top), imm(0x12345678));

	x86::Gp args_ptr = newUInt32("args_ptr");
	x86::Gp arg_count = newUInt32("arg_count");
	x86::Gp frame = getStackFramePtr();
	setArg(1, arg_count);
	setArg(2, args_ptr);
	x86::Gp copier = newUInt32("arg type");
	//todo: have the loop read the actual arg count and not the procnode one
	for (uint32_t i = 0; i < args_count; i++) // Copy args to stack frame
	{
		setInlineComment((std::string("mov arg ") + std::to_string(i)).c_str());
		mov(copier, x86::dword_ptr(args_ptr, i * sizeof(Value) + offsetof(Value, type)));
		mov(x86::dword_ptr(frame, sizeof(ProcStackFrame) + i * sizeof(Value) + offsetof(Value, type)), copier);
		mov(copier, x86::dword_ptr(args_ptr, i * sizeof(Value) + offsetof(Value, value)));
		mov(x86::dword_ptr(frame, sizeof(ProcStackFrame) + i * sizeof(Value) + offsetof(Value, value)), copier);
	}

	// Set src type and value
	x86::Gp copy_thingy = newUInt32("src type");
	x86::Gp copy_thingy2 = newUInt32("src value");
	setArg(3, copy_thingy);
	mov(x86::ptr(new_frame, offsetof(ProcStackFrame, src) + offsetof(Value, type)), copy_thingy);
	setArg(4, copy_thingy2);
	mov(x86::ptr(new_frame, offsetof(ProcStackFrame, src) + offsetof(Value, value)), copy_thingy2);

	// Set the current iterator to nullptr
	mov(x86::ptr(new_frame, offsetof(ProcStackFrame, current_iterator), sizeof(uint32_t)), Imm(0));

	// Set parent execution context
	/*x86::Gp parent_ctx_ptr = newUIntPtr("parent context pointer");
	setArg(7, parent_ctx_ptr);
	mov(x86::dword_ptr(getStackFramePtr(), offsetof(ProcStackFrame, caller_execution_context)), parent_ctx_ptr);*/

	// Default locals to null
	// Casting Imms to Gp is kinda gross but it works
	Variable null{Imm(DataType::NULL_D).as<x86::Gp>(), Imm(0).as<x86::Gp>()};
	for (uint32_t i = 0; i < locals_count; i++)
	{
		setLocal(i, null);
	}

	commitLocals();

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
	if (_currentProc->needs_sleep)
	{
		bind(_currentProc->_continuationPointTable);
		for (Label l : _currentProc->_continuationPoints)
		{
			embedLabelDelta(l, _currentProc->_continuationPointTable, 4);
		}
	}
	_currentProc = nullptr;
}

x86::Gp DMCompiler::getJitContext()
{
	return _currentProc->_jit_context;
}

unsigned int DMCompiler::addContinuationPoint()
{
	if (!_currentProc->needs_sleep)
	{
		return 0;
	}
	Label l = newLabel();
	bind(l);
	_currentProc->_cont_points_annotation->addLabel(l);
	_currentProc->_continuationPoints.append(&_allocator, l);
	return _currentProc->_continuationPoints.size() - 1;
}

unsigned int DMCompiler::prepareNextContinuationIndex()
{
	if (!_currentProc->needs_sleep)
	{
		return 0;
	}
	setInlineComment("Set current continuation point");
	mov(x86::ptr(getStackFramePtr(), offsetof(ProcStackFrame, continuation_index), sizeof(uint32_t)), imm(_currentProc->_continuationPoints.size()));
	return _currentProc->_continuationPoints.size();
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
	//mov(_currentBlock->_stack_top, x86::dword_ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top)));
	if (_currentProc->needs_sleep)
	{
		cmp(x86::byte_ptr(_currentProc->_jit_context, offsetof(JitContext, suspended)), imm(1));
		Label not_suspended = newLabel();
		jne(not_suspended);
		doYield();
		bind(not_suspended);
	}
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

	auto stack_frame = getStackFramePtr();

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

	auto stack_frame = getStackFramePtr();
	auto type = newUInt32("arg type");
	auto value = newUInt32("arg value");
	mov(type, x86::ptr(stack_frame, sizeof(ProcStackFrame) + sizeof(Value) * index, sizeof(uint32_t)));
	mov(value, x86::ptr(stack_frame, sizeof(ProcStackFrame) + sizeof(Value) * index + offsetof(Value, value), sizeof(uint32_t)));

	return { type, value };
}

void DMCompiler::setLocal(uint32_t index, const Variable& variable)
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
	const auto val = getStackFramePtr();
	const auto type = newUInt32("embedded_type");
	const auto value = newUInt32("embedded_value");
	lea(val, x86::ptr(val, offset));
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

void DMCompiler::setDot(const Variable& variable)
{
	auto dot = newUInt32("dot");
	lea(dot, x86::ptr(getStackFramePtr(), offsetof(ProcStackFrame, dot)));
	mov(x86::dword_ptr(dot, offsetof(Value, type)), variable.Type.as<x86::Gp>());
	mov(x86::dword_ptr(dot, offsetof(Value, value)), variable.Value.as<x86::Gp>());
}

void DMCompiler::pushStackRaw(const Variable& variable)
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
	var.Type = newInt32("push_stack_type");
	var.Value = newInt32("push_stack_value");
	pushStackRaw(var);
	return var;
}

Variable DMCompiler::pushStack(Operand type, Operand value)
{
	Variable var;
	var.Type = newInt32("push stack type");
	var.Value = newInt32("push stack value");
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

	x86::Gp stack_top = newUIntPtr("stack_top");
	mov(stack_top, x86::dword_ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top)));
	add(stack_top, block._stack_top_offset * sizeof(Value));

	size_t i = 0;
	while(!block._stack.empty())
	{
		Variable var = block._stack.pop();

		mov(x86::dword_ptr(stack_top,  -(i * sizeof(Value)) + offsetof(Value, type) - sizeof(Value)), var.Type.as<x86::Gp>());
		mov(x86::dword_ptr(stack_top,  -(i * sizeof(Value)) + offsetof(Value, value) - sizeof(Value)), var.Value.as<x86::Gp>());

		i++;
	}
	block._stack_top_offset = 0;
	mov(x86::dword_ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top)), stack_top);
}

void DMCompiler::commitLocals()
{
	if (_currentProc == nullptr)
		__debugbreak();
	ProcNode& proc = *_currentProc;

	setInlineComment("commitLocals");

	x86::Gp stack_frame = getStackFramePtr();

	for (uint32_t i = 0; i < proc._locals_count; i++)
	{
		Local& local = proc._locals[i];

		switch (local.State)
		{
		case Local::CacheState::Ok:
			break;
		case Local::CacheState::Modified:
			mov(x86::ptr(stack_frame, sizeof(ProcStackFrame) + sizeof(Value) * proc._args_count +  i * sizeof(Value) + offsetof(Value, type), sizeof(uint32_t)), local.Variable.Type.as<x86::Gp>());
			mov(x86::ptr(stack_frame, sizeof(ProcStackFrame) + sizeof(Value) * proc._args_count + i * sizeof(Value) + offsetof(Value, value), sizeof(uint32_t)), local.Variable.Value.as<x86::Gp>());

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
	//commitStack();
	//commitLocals();

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

x86::Gp DMCompiler::getStackFramePtr()
{
	if(_currentProc == nullptr)
		__debugbreak();
	x86::Gp stack_frame = newUIntPtr("stack_frame_ptr");
	mov(stack_frame, x86::dword_ptr(_currentProc->_jit_context, offsetof(JitContext, stack_frame)));
	return stack_frame;
}

x86::Gp DMCompiler::getCurrentIterator()
{
	if (_currentProc == nullptr)
		__debugbreak();
	x86::Gp current_iterator = newUIntPtr("current_iterator");
	x86::Gp frame = getStackFramePtr();
	mov(current_iterator, x86::dword_ptr(frame, offsetof(ProcStackFrame, current_iterator)));
	return current_iterator;
}

void DMCompiler::setCurrentIterator(Operand iter)
{
	if (_currentProc == nullptr)
		__debugbreak();

	x86::Gp frame = getStackFramePtr();
	mov(x86::dword_ptr(frame, offsetof(ProcStackFrame, current_iterator)), iter.as<x86::Gp>());
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

	commitStack();
	commitLocals();

	ProcNode& proc = *_currentProc;
	BlockNode& block = *_currentBlock;

	// Destroy the current iterator if there is one.
	auto iterator = getCurrentIterator();

	auto del_iter = call((uint32_t)delete_iterator, FuncSignatureT<void, DMListIterator*>());
	del_iter->setArg(0, iterator);

	xor_(iterator, iterator);

	// The only thing our proc should leave on the stack is the return value
	auto retval = popStack();
	
	setInlineComment("doReturn");

	x86::Gp frame = getStackFramePtr();

	x86::Gp stack_top = newUInt32();
	mov(stack_top, x86::dword_ptr(_currentProc->_jit_context, offsetof(JitContext, stack_top)));

	// Restore previous stack frame
	x86::Gp prev_stack_frame = newUIntPtr("prev_stack_frame");
	mov(prev_stack_frame, x86::ptr(frame, offsetof(ProcStackFrame, previous), sizeof(uint32_t)));
	mov(x86::dword_ptr(proc._jit_context, offsetof(JitContext, stack_frame)), prev_stack_frame);

	sub(stack_top, _currentProc->_locals_count * sizeof(Value) + _currentProc->_args_count * sizeof(Value) + sizeof(ProcStackFrame));
		
	// Move return value to stack
	mov(x86::ptr(stack_top, offsetof(Value, type), sizeof(uint32_t)), retval.Type.as<x86::Gp>());
	mov(x86::ptr(stack_top, offsetof(Value, value), sizeof(uint32_t)), retval.Value.as<x86::Gp>());

	add(stack_top, sizeof(Value));
	mov(x86::dword_ptr(proc._jit_context, offsetof(JitContext, stack_top)), stack_top);
	
	// return Procresult::Success
	_return(ProcResult::Success);
}

void DMCompiler::_return(ProcResult code)
{
	x86::Gp retcode = newUInt32("retcode");
	mov(retcode, Imm(static_cast<uint32_t>(code)));
	ret(retcode);
}

void DMCompiler::doYield()
{
	pushStackRaw(getDot());
	prepareNextContinuationIndex();
	commitStack();
	commitLocals();
	_return(ProcResult::Yielded);
	addContinuationPoint();
}

void DMCompiler::doSleep()
{
	pushStackRaw(getDot());
	prepareNextContinuationIndex();
	commitStack();
	commitLocals();
	_return(ProcResult::Sleeping);
	addContinuationPoint();
}

}