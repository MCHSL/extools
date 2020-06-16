#pragma once

#include "../../third_party/asmjit/asmjit.h"
#include "DMCompiler.h"

#include <vector>
#include <set>

namespace jit
{

using namespace asmjit;

class DMCompiler;
class BlockNode;

struct VirtualRegisterInfo
{
	uint32_t index;
	uint32_t group;

	// The first time we were referenced
	uint32_t startpoint;

	// The last time we were referenced
	uint32_t endpoint;

	// The physical register we currently own
	std::optional<uint32_t> phys_id;
};

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

private:
	struct PhysicalRegister
	{
		uint32_t id;
		uint32_t group;
	};

	std::vector<InstNode*> GatherInstructions(BlockNode* block, Zone* zone, Logger* logger);

	std::vector<VirtualRegisterInfo> CreateVirtualRegistersOrderedByStartPoint(const std::vector<InstNode*>& instructions);

	void Apply(std::vector<InstNode*> instructions, std::vector<VirtualRegisterInfo> registers);
};

}