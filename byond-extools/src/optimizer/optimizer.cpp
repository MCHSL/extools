#define NOMINMAX
#include "optimizer.h"
#include "../dmdism/disassembly.h"
#include "../dmdism/opcodes_enum.h"
#include <fstream>
#include <algorithm>

std::map<Core::Proc*, bool> has_been_optimized;
std::vector<Core::Proc*> inlineable_procs;

void inline_into(Disassembly& recipient, Disassembly& donor, int which_instruction, int local_count)
{
	int arg_count = 0;
	for (Instruction& i : donor)
	{
		if (i == Bytecode::GETVAR && i.bytes().at(1) == (std::uint32_t) AccessModifier::ARG)
		{
			arg_count = std::max(arg_count, (int)i.bytes().at(2)+1);
		}
	}
	std::vector<Instruction> plop_after_inlining = std::vector<Instruction>(recipient.instructions.begin() + which_instruction + 1, recipient.instructions.end());
	recipient.instructions.resize(which_instruction+1);
	int starting_bytecount = recipient.bytecount();
	recipient.instructions.pop_back();
	if (arg_count)
	{
		for (int i = arg_count-1; i >= 0; i--)
		{
			Instruction instr { Bytecode::SETVAR };
			instr.add_byte((std::uint32_t) AccessModifier::LOCAL);
			instr.add_byte(local_count + i + 1);
			recipient.instructions.push_back(instr);
		}
	}
	std::vector<int> return_jump_patch_locations;
	for (int i=0; i<donor.size(); i++)
	{
		Instruction instr = donor.at(i);
		if (instr == Bytecode::DBG_LINENO || instr == Bytecode::DBG_FILE)
		{
			continue;
		}
		if (instr == Bytecode::END || instr == Bytecode::RET)
		{
			instr = Instruction { Bytecode::JMP };
			instr.add_byte(0);
			return_jump_patch_locations.push_back(recipient.instructions.size());
		}
		if (instr == Bytecode::GETVAR || instr == Bytecode::SETVAR)
		{
			if (instr.bytes()[1] == (std::uint32_t) AccessModifier::LOCAL)
			{
				instr.bytes()[2] += local_count;
			}
			else if (instr.bytes().at(1) == (std::uint32_t) AccessModifier::ARG)
			{
				instr.bytes().at(1) = (std::uint32_t) AccessModifier::LOCAL;
				instr.bytes().at(2) += local_count + 1;
			}
		}
		if (instr == Bytecode::JMP || instr == Bytecode::JMP2 || instr == Bytecode::JZ || instr == Bytecode::JNZ || instr == Bytecode::FOR_RANGE)
		{
			instr.bytes().at(1) += which_instruction;
		}
		recipient.instructions.push_back(instr);
	}
	recipient.instructions.pop_back(); //Skip the implicit END
	return_jump_patch_locations.pop_back();
	if (recipient.instructions.back() == Bytecode::JMP)
	{
		recipient.instructions.pop_back(); //if there was a return, also skip it
		return_jump_patch_locations.pop_back();
	}
	recipient.recalculate_offsets();
	for (int instr_index : return_jump_patch_locations)
	{
		recipient.at(instr_index).bytes().at(1) = recipient.instructions.back().offset() + 2;
	}
	for (int i = 0; i < recipient.size()-1; i++)
	{
		Instruction& instr = recipient.at(i);
		if (instr == Bytecode::JMP || instr == Bytecode::JMP2 || instr == Bytecode::JZ || instr == Bytecode::JNZ)
		{
			if (instr.bytes().at(1) == recipient.at(i + 1).offset())
			{
				recipient.instructions.erase(recipient.instructions.begin() + i);
			}
		}
	}
	int added_instructions = recipient.bytecount() - starting_bytecount;
	for (int i = 0; i < which_instruction; i++)
	{
		Instruction& instr = recipient.at(i);
		if (instr == Bytecode::JMP || instr == Bytecode::JMP2 || instr == Bytecode::JZ || instr == Bytecode::JNZ || instr == Bytecode::FOR_RANGE)
		{
			instr.bytes().at(1) += added_instructions;
		}
	}
	for (Instruction& instr : plop_after_inlining)
	{
		if (instr == Bytecode::JMP || instr == Bytecode::JMP2 || instr == Bytecode::JZ || instr == Bytecode::JNZ || instr == Bytecode::FOR_RANGE)
		{
			if (instr.bytes().at(1) <= starting_bytecount)
			{
				continue;
			}
			instr.bytes().at(1) += added_instructions;
		}
	}
	recipient.instructions.insert(recipient.instructions.end(), plop_after_inlining.begin(), plop_after_inlining.end());
}

void optimize_proc(Core::Proc& recipient);

void optimize_inline(Core::Proc& recipient, Disassembly& recipient_code)
{
	for (int i = 0; i < recipient_code.size(); i++)
	{
		Instruction& instr = recipient_code.at(i);
		if (instr == Bytecode::CALLGLOB)
		{
			Core::Proc& donor = Core::get_proc(instr.bytes()[2]);
			if (donor == recipient)
			{
				continue;
			}
			optimize_proc(donor);
			Disassembly d = donor.disassemble();
			inline_into(recipient_code, d, i, recipient.get_local_count());
			i = 0;
		}
	}
}

void optimize_proc(Core::Proc& victim)
{
	if (has_been_optimized[&victim])
	{
		return;
	}
	Disassembly victim_code = victim.disassemble();
	optimize_inline(victim, victim_code);
	victim.assemble(victim_code);
	has_been_optimized[&victim] = true;
}

void dump_proc_to_file(Core::Proc p, std::string name)
{
	//Core::Alert("Dumping");
	std::ofstream fout(name);
	Disassembly dis = p.disassemble();
	for (Instruction& i : dis)
	{
		fout << i.offset() << " " << i.opcode().tostring() << std::endl;
	}
}

void optimizer_initialize()
{
	std::vector<Core::Proc>& all_procs = Core::get_all_procs();
	for (Core::Proc& p : all_procs)
	{
		has_been_optimized[&p] = false;
		if (!p.raw_path.empty() && p.raw_path.back() != ')' && p.raw_path.rfind("/proc/", 0) == 0) //find all global procs
		{
			inlineable_procs.push_back(&p);
		}
	}
	for (Core::Proc& p : all_procs)
	{
		optimize_proc(p);
	}
	//Core::Proc p = Core::get_proc("/proc/bench_cheap_hypotenuse_no_abs");
	//dump_proc_to_file(Core::get_proc("/proc/cheap_hypotenuse_no_abs"), "hypot.txt");
	//dump_proc_to_file(p, "before.txt");
	//optimize_proc(p);
	//dump_proc_to_file(p, "after.txt");
}