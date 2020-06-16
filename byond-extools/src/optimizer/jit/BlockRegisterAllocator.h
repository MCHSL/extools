#pragma once

#include "../../third_party/asmjit/asmjit.h"
#include "DMCompiler.h"

namespace jit
{

using namespace asmjit;

class DMCompiler;
class BlockNode;

class BlockRegisterAllocator
	: public Pass
{
public:
	BlockRegisterAllocator()
		: Pass("BlockRegisterAllocator")
	{}

	inline DMCompiler* dmc() const noexcept { return static_cast<DMCompiler*>(_cb); }

	Error run(Zone* zone, Logger* logger) override;
	void visitBlock(BlockNode* block, Zone* zone, Logger* logger);
};

}