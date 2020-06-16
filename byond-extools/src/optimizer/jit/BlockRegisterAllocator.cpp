#include "BlockRegisterAllocator.h"
#include "JitContext.h"

#include <array>

namespace jit
{

using namespace asmjit;

// Current allocation state of a physical register
struct RegisterState
{
	x86::Reg reg;
	bool active;
};

Error BlockRegisterAllocator::run(Zone* zone, Logger* logger)
{
	BaseNode* node = cb()->firstNode();
	if (!node)
	{
		return kErrorOk;
	}

	do
	{
		if (node->type() == static_cast<uint32_t>(NodeTypes::kNodeBlock))
		{
			BlockNode* block = node->as<BlockNode>();
			node = block->_end;
			visitBlock(block, zone, logger);
		}

		node = node->next();
	} while (node);

	return kErrorOk;
}

void BlockRegisterAllocator::visitBlock(BlockNode* block, Zone* zone, Logger* logger)
{
	// This sucks. Order matters. Whole thing matters!!!
	// Pre-activates eax and esp because I've reserved them.
	// physical_register_groups[x86::Reg::kGroupGp], physical_register_groups[x86::Reg::kGroupVec]
	std::array<std::vector<RegisterState>, 2> physical_register_groups{{
		{{x86::eax, false}, {x86::ecx, false}, {x86::edx, false}, {x86::ebx, false}, {x86::esp, false}, {x86::ebp, false}, {x86::esi, false}, {x86::edi, false}},
		{{x86::xmm0, false}, {x86::xmm1, false}, {x86::xmm2, false}, {x86::xmm3, false}, {x86::xmm4, false}, {x86::xmm5, false}, {x86::xmm6, false}, {x86::xmm7, false}}
	}};

	// eax and esp are reserved for our other code (yes there's no pretty API here to do this)
	physical_register_groups[x86::Reg::kGroupGp][x86::Gp::kIdAx].active = true;
	physical_register_groups[x86::Reg::kGroupGp][x86::Gp::kIdSp].active = true;

	// Get all of the instructions into a nice vector
	std::vector<InstNode*> instructions = GatherInstructions(block, zone, logger);	

	// NOTE: We're gonna use uint32_t as register groups & IDs a lot here. They're useful because they can index our above array.

	// We need a list of every virtual registered ordered by first reference
	std::vector<VirtualRegisterInfo> registers = CreateVirtualRegistersOrderedByStartPoint(instructions);

	// indices to registers
	std::vector<size_t> active;

	for (size_t info_index = 0; info_index < registers.size(); info_index++)
	{
		auto& info = registers[info_index];

		// Free any active registers no longer being used
		auto active_it = active.begin();
		while (active_it != active.end())
		{
			VirtualRegisterInfo& active_info = registers[*active_it];
			if (active_info.endpoint >= info.startpoint)
				break;

			// Mark the register as available
			physical_register_groups[active_info.group][*active_info.phys_id].active = false;

			// Remove us from active
			active_it = active.erase(active_it);
		}

		auto& physical_registers = physical_register_groups[info.group];

		// Find a free register for this var
		auto allocated_register = std::find_if(physical_registers.begin(), physical_registers.end(),
			[&info](const RegisterState& v) { return v.active == false; });

		if (allocated_register == physical_registers.end())
		{
			// TODO: Spill
			__debugbreak();
		}

		info.phys_id = allocated_register->reg.id();
		allocated_register->active = true;

		// Mark us as active, the vector has to remain sorted by ascending endpoint
		active.insert(std::upper_bound(active.begin(), active.end(), info.endpoint,
			[this, &registers](const uint32_t& endpoint, const uint32_t& index) { return endpoint < registers[index].endpoint; }), info_index);
	}

	Apply(instructions, registers);
}

std::vector<InstNode*> BlockRegisterAllocator::GatherInstructions(BlockNode* block, Zone* zone, Logger* logger)
{
	std::vector<InstNode*> instructions;

	BaseNode* node = block->next();
	do
	{
		if (node->isInst())
		{
			// TODO: Nice big error if a virtual register is referenced in two different blocks
			instructions.push_back(node->as<InstNode>());
		}

		node = node->next();
	} while(node && node->type() != static_cast<uint32_t>(NodeTypes::kNodeBlockEnd));

	return instructions;
}

std::vector<VirtualRegisterInfo> BlockRegisterAllocator::CreateVirtualRegistersOrderedByStartPoint(const std::vector<InstNode*>& instructions)
{
	std::vector<VirtualRegisterInfo> result;
	std::map<std::pair<uint32_t, uint32_t>, size_t> seen;

	auto visitOperand = [&seen, &result](uint32_t group, uint32_t index, uint32_t counter)
	{
		auto it = seen.find({group, index});

		// We've already seen this one, update its endpoint instead
		if (it != seen.end())
		{
			result[it->second].endpoint = counter;
			return;
		}

		VirtualRegisterInfo state;
		state.index = index;
		state.group = group;
		state.startpoint = counter;
		state.endpoint = counter;

		seen[{group, index}] = result.size();
		result.push_back(state);
	};

	for (size_t counter = 0; counter < instructions.size(); counter++)
	{
		const auto& inst = instructions[counter];

		for (uint32_t op_index = 0; op_index < inst->opCount(); op_index++)
		{
			auto& op = inst->op(op_index);

			// We only have to worry about registers and memory references to registers
			if (op.isVirtReg())
			{
				uint32_t virtIndex = Operand::virtIdToIndex(op.id());
				auto declaration = dmc()->_virtualRegisters.at(virtIndex);
				visitOperand(declaration->_info.group(), virtIndex, counter);
			}

			if (op.isMem())
			{
				x86::Mem& mem = op.as<x86::Mem>();

				if (mem.hasBaseReg() && Operand::isVirtId(mem.baseId()))
				{
					uint32_t virtIndex = Operand::virtIdToIndex(mem.baseId());
					auto declaration = dmc()->_virtualRegisters.at(virtIndex);
					visitOperand(declaration->_info.group(), virtIndex, counter);
				}

				if (mem.hasIndexReg() && Operand::isVirtId(mem.indexId()))
				{
					uint32_t virtIndex = Operand::virtIdToIndex(mem.indexId());
					auto declaration = dmc()->_virtualRegisters.at(virtIndex);
					visitOperand(declaration->_info.group(), virtIndex, counter);
				}
			}
		}
	}

	return result;
}

void BlockRegisterAllocator::Apply(std::vector<InstNode*> instructions, std::vector<VirtualRegisterInfo> registers)
{
	std::map<uint32_t, uint32_t> allocations;

	for (auto& info : registers)
	{
		jit::VirtualRegister* declaration = dmc()->_virtualRegisters.at(info.index);
		allocations[info.index] = *info.phys_id;
	}

	for (auto& inst : instructions)
	{
		for (uint32_t op_index = 0; op_index < inst->opCount(); op_index++)
		{
			auto& op = inst->op(op_index);
			if (op.isVirtReg())
			{
				x86::Reg& reg = op.as<x86::Reg>();
				uint32_t virtIndex = Operand::virtIdToIndex(op.id());
				reg.setId(allocations.at(virtIndex));				
			}

			if (op.isMem())
			{
				x86::Mem& mem = op.as<x86::Mem>();

				if (mem.hasBaseReg() && Operand::isVirtId(mem.baseId()))
				{
					uint32_t virtIndex = Operand::virtIdToIndex(mem.baseId());
					mem.setBaseId(allocations.at(virtIndex));
				}

				if (mem.hasIndexReg() && Operand::isVirtId(mem.indexId()))
				{
					uint32_t virtIndex = Operand::virtIdToIndex(mem.indexId());
					mem.setIndexId(allocations.at(virtIndex));
				}
			}
		}
	}
}

}