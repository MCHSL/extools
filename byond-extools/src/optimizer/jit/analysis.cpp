#include "analysis.h"

#include <map>
#include <optional>
#include <set>
#include <fstream>

#include "DMCompiler.h"
#include "../../core/core.h"
#include "../../dmdism/disassembly.h"

std::map<unsigned int, AnalysisResult> saved_analyses;
static std::ofstream blocks_out("blocks.txt");

std::optional<AnalysisResult> analyze_proc(const Core::Proc& proc)
{
	AnalysisResult result;
	auto& blocks = result.blocks;
	Disassembly dis = proc.disassemble();
	
	unsigned int current_block_offset = 0;
	blocks[current_block_offset] = ProcBlock(0);
	std::set<unsigned int> jump_targets;
	for (Instruction& i : dis)
	{
		if (i == Bytecode::SETVAR || i == Bytecode::GETVAR)
		{
			switch (i.bytes()[1])
			{
			case AccessModifier::LOCAL:
				result.local_count = std::max(result.local_count, i.bytes()[2] + 1);
				break;
			case AccessModifier::ARG:
				result.argument_count = std::max(result.argument_count, i.bytes()[2] + 1);
				break;
			case AccessModifier::SUBVAR:
				if (i.acc_base.first == AccessModifier::LOCAL)
				{
					result.local_count = std::max(result.local_count, i.acc_base.second + 1);
				}
				else if (i.acc_base.first == AccessModifier::ARG)
				{
					result.argument_count = std::max(result.argument_count, i.acc_base.second + 1);
				}
			default:
				break;
			}
		}

		if (jump_targets.find(i.offset()) != jump_targets.end())
		{
			current_block_offset = i.offset();
			blocks[current_block_offset] = ProcBlock(current_block_offset);
			jump_targets.erase(i.offset());
		}

		blocks[current_block_offset].contents.push_back(i);
		if (i == Bytecode::JUMP_FALSE || i == Bytecode::JMP || i == Bytecode::JMP2 || i == Bytecode::JUMP_TRUE || i == Bytecode::JUMP_TRUE2 || i == JMP_AND || i == JMP_OR)
		{
			const unsigned int target = i.jump_locations().at(0);
			if (blocks.find(target) == blocks.end())
			{
				if (target > i.offset())
				{
					jump_targets.insert(i.jump_locations().at(0));
				}
				else //we need to split a block in twain
				{
					ProcBlock& victim = (--blocks.lower_bound(target))->second; //get the block that contains the target offset
					const auto split_point = std::find_if(victim.contents.begin(), victim.contents.end(), [target](Instruction& instr) { return instr.offset() == target; }); //find the target instruction
					ProcBlock new_block = ProcBlock(target);
					new_block.contents = std::vector<Instruction>(split_point, victim.contents.end());
					victim.contents.erase(split_point, victim.contents.end()); //split
					blocks[target] = new_block;
				}
			}
			current_block_offset = i.offset() + i.size();
			blocks[current_block_offset] = ProcBlock(current_block_offset);
		}
		else if (i == Bytecode::SLEEP)
		{
			result.needs_sleep = true;
			current_block_offset = i.offset() + i.size();
			blocks[current_block_offset] = ProcBlock(current_block_offset);
		}
		else if (i == Bytecode::CALLGLOB)
		{
			if (!result.needs_sleep)
			{
				auto jitted_it = saved_analyses.find(i.bytes().at(2));
				if (jitted_it != saved_analyses.end())
				{
					result.needs_sleep = jitted_it->second.needs_sleep;
				}
			}
			current_block_offset = i.offset() + i.size();
			blocks[current_block_offset] = ProcBlock(current_block_offset);
		}
		else if (i == Bytecode::CALL || i == Bytecode::CALLNR)
		{
			result.needs_sleep = true;
			current_block_offset = i.offset() + i.size();
			blocks[current_block_offset] = ProcBlock(current_block_offset);
			if (i.bytes()[1] == SUBVAR)
			{
				if (i.acc_base.first == AccessModifier::LOCAL)
				{
					result.local_count = std::max(result.local_count, i.acc_base.second + 1);
				}
				else if (i.acc_base.first == AccessModifier::ARG)
				{
					result.argument_count = std::max(result.argument_count, i.acc_base.second + 1);
				}
			}
		}
	}

	blocks_out << "BEGIN: " << dis.proc->name << '\n';
	for (auto& [offset, block] : blocks)
	{
		blocks_out << std::hex << offset << ":\n";
		for (const Instruction& i : block.contents)
		{
			blocks_out << std::hex << i.offset() << std::dec << "\t\t\t" << i.bytes_str() << "\t\t\t" << i.opcode().mnemonic() << "\n";
		}
		blocks_out << "\n";
	}
	blocks_out << std::endl;

	saved_analyses[proc.id] = result;
	return result;
}