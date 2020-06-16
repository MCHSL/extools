#pragma once

#include "../../core/core.h"
#include "../../third_party/asmjit/asmjit.h"

#include <stdint.h>
#include <vector>

namespace jit
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

class BlockNode
	: public BaseNode
{
public:
	BlockNode(BaseBuilder* cb, uint32_t locals_count, bool is_first)
		: BaseNode(cb, static_cast<uint32_t>(NodeTypes::kNodeBlock), kFlagHasNoEffect)
		, _locals_count(locals_count)
		, _end(nullptr)
	{
		_stack.init(&cb->_allocator);

		// Allocate space for all of our locals
		_locals = cb->_allocator.allocT<Local>(locals_count * sizeof(Local));

		// Make sure that:
		// 1) The first block always writes out the default null locals on block exit
		// 2) Any later blocks always read in the old values from the stack when necessary
		Local default_local {is_first ? Local::CacheState::Modified : Local::CacheState::Stale, {Imm(DataType::NULL_D), Imm(0)}};
		for (uint32_t i = 0; i < locals_count; i++)
		{
			_locals[i] = default_local;
		}

		cb->_newNodeT<BlockEndNode>(&_end);
	}

	BlockEndNode* _end;

	ZoneStack<Variable> _stack;

	Local* _locals;
	uint32_t _locals_count;
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
		, _prolog(cb->newLabel())
		, _epilog(cb->newLabel())
		, _locals_count(locals_count)
		, _end(nullptr)
	{
		cb->_newNodeT<ProcEndNode>(&_end);
		_blocks.reset();
	}

	// Label to our DM prologue
	Label _prolog;

	// Label to our DM epilogue
	Label _epilog;

	// The number of local (`var/x`) variables we will encounter
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

struct VirtualRegister
{
	VirtualRegister(uint32_t id, RegInfo& info, uint32_t size, uint32_t typeId)
		: _id(id)
		, _size(size)
		, _typeId(typeId)
	{
		_info.setSignature(info.signature());
	}

	VirtualRegister(const VirtualRegister&) = delete;
	VirtualRegister& operator=(const VirtualRegister&) = delete;

	uint32_t _id;
	RegInfo _info;
	uint32_t _size; // can be smaller than _info.size()
	uint32_t _typeId;
};

class DMCompiler
	: public x86::Builder
{
public:
	DMCompiler(CodeHolder& holder);
	

	//

public:

	ProcNode* addProc(uint32_t locals_count);
	void endProc();

	BlockNode* addBlock();
	void endBlock();

	// Commit our stack and locals to memory
	void commit();

	Variable getLocal(uint32_t index);
	void setLocal(uint32_t index, Variable& variable);

	Variable popStack();
	void pushStack(Variable& variable);

private:
	ProcNode* _currentProc;
	BlockNode* _currentBlock;




	//
	// Virtual Registers
	//
public:
	BaseReg DMCompiler::newRegister(uint32_t typeId);

private:
	VirtualRegister* newVirtualRegister(uint32_t typeId, RegInfo& info);
	ZoneVector<VirtualRegister*> _virtualRegisters;

public:
#define ASMJIT_NEW_REG_TYPED(FUNC, REG, TYPE_ID) \
    inline REG FUNC() {                          \
      return newRegister(TYPE_ID).as<REG>();     \
    }

	ASMJIT_NEW_REG_TYPED(newI8     , asmjit::x86::Gp  , asmjit::Type::kIdI8     )
	ASMJIT_NEW_REG_TYPED(newU8     , asmjit::x86::Gp  , asmjit::Type::kIdU8     )
	ASMJIT_NEW_REG_TYPED(newI16    , asmjit::x86::Gp  , asmjit::Type::kIdI16    )
	ASMJIT_NEW_REG_TYPED(newU16    , asmjit::x86::Gp  , asmjit::Type::kIdU16    )
	ASMJIT_NEW_REG_TYPED(newI32    , asmjit::x86::Gp  , asmjit::Type::kIdI32    )
	ASMJIT_NEW_REG_TYPED(newU32    , asmjit::x86::Gp  , asmjit::Type::kIdU32    )
	ASMJIT_NEW_REG_TYPED(newI64    , asmjit::x86::Gp  , asmjit::Type::kIdI64    )
	ASMJIT_NEW_REG_TYPED(newU64    , asmjit::x86::Gp  , asmjit::Type::kIdU64    )
	ASMJIT_NEW_REG_TYPED(newInt8   , asmjit::x86::Gp  , asmjit::Type::kIdI8     )
	ASMJIT_NEW_REG_TYPED(newUInt8  , asmjit::x86::Gp  , asmjit::Type::kIdU8     )
	ASMJIT_NEW_REG_TYPED(newInt16  , asmjit::x86::Gp  , asmjit::Type::kIdI16    )
	ASMJIT_NEW_REG_TYPED(newUInt16 , asmjit::x86::Gp  , asmjit::Type::kIdU16    )
	ASMJIT_NEW_REG_TYPED(newInt32  , asmjit::x86::Gp  , asmjit::Type::kIdI32    )
	ASMJIT_NEW_REG_TYPED(newUInt32 , asmjit::x86::Gp  , asmjit::Type::kIdU32    )
	ASMJIT_NEW_REG_TYPED(newInt64  , asmjit::x86::Gp  , asmjit::Type::kIdI64    )
	ASMJIT_NEW_REG_TYPED(newUInt64 , asmjit::x86::Gp  , asmjit::Type::kIdU64    )
	ASMJIT_NEW_REG_TYPED(newIntPtr , asmjit::x86::Gp  , asmjit::Type::kIdIntPtr )
	ASMJIT_NEW_REG_TYPED(newUIntPtr, asmjit::x86::Gp  , asmjit::Type::kIdUIntPtr)

	ASMJIT_NEW_REG_TYPED(newGpb    , asmjit::x86::Gp  , asmjit::Type::kIdU8     )
	ASMJIT_NEW_REG_TYPED(newGpw    , asmjit::x86::Gp  , asmjit::Type::kIdU16    )
	ASMJIT_NEW_REG_TYPED(newGpd    , asmjit::x86::Gp  , asmjit::Type::kIdU32    )
	ASMJIT_NEW_REG_TYPED(newGpq    , asmjit::x86::Gp  , asmjit::Type::kIdU64    )
	ASMJIT_NEW_REG_TYPED(newGpz    , asmjit::x86::Gp  , asmjit::Type::kIdUIntPtr)
	ASMJIT_NEW_REG_TYPED(newXmm    , asmjit::x86::Xmm , asmjit::Type::kIdI32x4  )
	ASMJIT_NEW_REG_TYPED(newXmmSs  , asmjit::x86::Xmm , asmjit::Type::kIdF32x1  )
	ASMJIT_NEW_REG_TYPED(newXmmSd  , asmjit::x86::Xmm , asmjit::Type::kIdF64x1  )
	ASMJIT_NEW_REG_TYPED(newXmmPs  , asmjit::x86::Xmm , asmjit::Type::kIdF32x4  )
	ASMJIT_NEW_REG_TYPED(newXmmPd  , asmjit::x86::Xmm , asmjit::Type::kIdF64x2  )
	ASMJIT_NEW_REG_TYPED(newYmm    , asmjit::x86::Ymm , asmjit::Type::kIdI32x8  )
	ASMJIT_NEW_REG_TYPED(newYmmPs  , asmjit::x86::Ymm , asmjit::Type::kIdF32x8  )
	ASMJIT_NEW_REG_TYPED(newYmmPd  , asmjit::x86::Ymm , asmjit::Type::kIdF64x4  )
	ASMJIT_NEW_REG_TYPED(newZmm    , asmjit::x86::Zmm , asmjit::Type::kIdI32x16 )
	ASMJIT_NEW_REG_TYPED(newZmmPs  , asmjit::x86::Zmm , asmjit::Type::kIdF32x16 )
	ASMJIT_NEW_REG_TYPED(newZmmPd  , asmjit::x86::Zmm , asmjit::Type::kIdF64x8  )
	ASMJIT_NEW_REG_TYPED(newMm     , asmjit::x86::Mm  , asmjit::Type::kIdMmx64  )
	ASMJIT_NEW_REG_TYPED(newKb     , asmjit::x86::KReg, asmjit::Type::kIdMask8  )
	ASMJIT_NEW_REG_TYPED(newKw     , asmjit::x86::KReg, asmjit::Type::kIdMask16 )
	ASMJIT_NEW_REG_TYPED(newKd     , asmjit::x86::KReg, asmjit::Type::kIdMask32 )
	ASMJIT_NEW_REG_TYPED(newKq     , asmjit::x86::KReg, asmjit::Type::kIdMask64 )

#undef ASMJIT_NEW_REG_TYPED

};

}