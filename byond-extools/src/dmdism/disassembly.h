#pragma once

#include <vector>
#include "instruction.h"
#include "../core/proc_management.h"

class Disassembly
{
public:
	Disassembly(std::vector<Instruction>&& i) : instructions(std::move(i)) {}
	//~Disassembly() { for (auto i : instructions) delete& i; } //heap corruption woo
	std::vector<Instruction> instructions;
	Core::Proc* proc;

	std::vector<std::uint32_t> assemble();
	Instruction& at(std::size_t i);
	Instruction& at_offset(std::size_t offset);
	Instruction* next_from_offset(std::uint16_t offset);
	std::uint32_t op_at(std::size_t i);
	std::vector<Instruction>::iterator begin() noexcept;
	std::vector<Instruction>::iterator end() noexcept;
	void insert_at(std::size_t at, Instruction instr);
	void add_byte_to_last(std::uint32_t byte);
	void recalculate_offsets();
	std::size_t size();
	std::size_t bytecount();

	static Disassembly from_proc(Core::Proc& proc);
};
