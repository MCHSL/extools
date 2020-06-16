#include "DMCompiler.h"
#include "JitContext.h"
#include "BlockRegisterAllocator.h"

#include <algorithm>

using namespace asmjit;

namespace jit
{

DMCompiler::DMCompiler(asmjit::CodeHolder& holder)
	: asmjit::x86::Builder(&holder)
	, _currentProc(nullptr)
	, _currentBlock(nullptr)
{
	

	// Register allocation runs last to be sure that registers used by the other passes get translated
	addPassT<BlockRegisterAllocator>();
}

//
/// Virtual Registers
//

VirtualRegister* DMCompiler::newVirtualRegister(uint32_t typeId, RegInfo& info)
{
	uint32_t index = _virtualRegisters.size();

	VirtualRegister* reg = _allocator.allocZeroedT<VirtualRegister>();
	uint32_t size = asmjit::Type::sizeOf(typeId);

	return new(reg) VirtualRegister(Operand::indexToVirtId(index), info, size, typeId);
}

BaseReg DMCompiler::newRegister(uint32_t typeId)
{
	RegInfo info;
	if (ArchUtils::typeIdToRegInfo(arch(), typeId, &typeId, &info) != kErrorOk)
		__debugbreak();

	VirtualRegister* reg = newVirtualRegister(typeId, info);
	return BaseReg(typeId, reg->_id);
}

ProcNode* DMCompiler::addProc(uint32_t locals_count)
{
	if (_currentProc != nullptr)
		__debugbreak();
	_newNodeT<ProcNode>(&_currentProc, locals_count);
	addNode(_currentProc);
	return _currentProc;
}

void DMCompiler::endProc()
{
	if (_currentBlock != nullptr)
		__debugbreak();
	if (_currentProc == nullptr)
		__debugbreak();
	addNode(_currentProc->_end);
	_currentProc = nullptr;
}

BlockNode* DMCompiler::addBlock()
{
	if (_currentProc == nullptr)
		__debugbreak();
	if (_currentBlock != nullptr)
		__debugbreak();
	_newNodeT<BlockNode>(&_currentBlock, _currentProc->_locals_count, _currentProc->_blocks.empty());
	_currentProc->_blocks.append(&_allocator, _currentBlock);
	addNode(_currentBlock);
	return _currentBlock;
}

void DMCompiler::endBlock()
{
	if (_currentBlock == nullptr)
		__debugbreak();
	addNode(_currentBlock->_end);
	_currentBlock = nullptr;
}

void DMCompiler::commit()
{
	if (_currentBlock == nullptr)
		__debugbreak();
		/*
	ProcNode& proc = *_currentProc;

	for (uint32_t i = 0; i < proc._locals_count; i++)
	{
		proc._locals[i].Valid = false;
	}

	proc._stack.init(&_allocator);
	*/
}

Variable DMCompiler::getLocal(uint32_t index)
{
	if (_currentBlock == nullptr)
		__debugbreak();
	BlockNode& block = *_currentBlock;
	if (index >= block._locals_count)
		__debugbreak();

	Local& local = block._locals[index];

	switch (local.State)
	{
	case Local::CacheState::Ok:
	case Local::CacheState::Modified:
		return local.Variable;
	case Local::CacheState::Stale:
	default:
		break;
	}

	// We need to fetch the data from the JitContext
	auto stack_frame = newUInt32();
	mov(stack_frame, x86::ptr(x86::eax, offsetof(JitContext, stack_frame), sizeof(intptr_t)));

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
	if (_currentBlock == nullptr)
		__debugbreak();
	BlockNode& block = *_currentBlock;
	if (index >= block._locals_count)
		__debugbreak();
	block._locals[index] = {Local::CacheState::Modified, variable};
}

Variable DMCompiler::popStack()
{
	if (_currentBlock == nullptr)
		__debugbreak();
	BlockNode& block = *_currentBlock;

	// The stack cache could be empty if something was pushed to it before jumping to a new block
	if (!block._stack.empty())
	{
		return block._stack.pop();
	}

	auto stack_top = newUInt32();
	mov(stack_top, x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)));

	auto type = newUInt32();
	auto value = newUInt32();
	mov(type, x86::ptr(stack_top, -sizeof(Value), sizeof(uint32_t)));
	mov(value, x86::ptr(stack_top, -(sizeof(value) / 2), sizeof(uint32_t)));
	return {type, value};
}

void DMCompiler::pushStack(Variable& variable)
{
	if (_currentBlock == nullptr)
		__debugbreak();
	BlockNode& block = *_currentBlock;
	_currentBlock->_stack.append(variable);
}

}