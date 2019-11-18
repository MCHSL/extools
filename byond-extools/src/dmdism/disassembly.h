#pragma once

#include <vector>
#include "instruction.h"
#include "../core/proc_management.h"

class Disassembly
{
public:
	Disassembly(std::vector<Instruction> i) : instructions(i) {}
	std::vector<Instruction> instructions;
	Core::Proc proc;

	std::vector<int>* assemble();
	Instruction& at(unsigned int i);
	Instruction& at_offset(unsigned int offset);
	Instruction* next_from_offset(unsigned int offset);
	int op_at(unsigned int i);
	std::vector<Instruction>::iterator begin() noexcept;
	std::vector<Instruction>::iterator end() noexcept;
	void insert_at(unsigned int at, Instruction instr);
	void add_byte_to_last(unsigned int byte);
	void recalculate_offsets();
	std::size_t size();
	int bytecount();
};

namespace Core
{
	Disassembly get_disassembly(std::string proc);
}