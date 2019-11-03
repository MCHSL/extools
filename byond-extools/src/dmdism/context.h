#pragma once

#include <vector>

#include "../core/proc_management.h"
#include "instruction.h"

class Context
{
public:
	Context(std::vector<std::uint32_t> bc, std::vector<Core::Proc>& ps) : buffer_(bc), procs_(ps) {}

	std::vector<std::uint32_t> buffer() const { return buffer_; }
	std::vector<Core::Proc> procs() const { return procs_; }
	bool more() const { return current_offset_ < buffer_.size(); }

	std::uint32_t peek();
	std::uint32_t eat();
	std::uint32_t eat_add();

	void set_instr(Instruction* instr);
	void finish_instr();

private:
	std::vector<std::uint32_t> buffer_;
	std::uint32_t last_opcode_ = 0;
	std::uint32_t current_offset_ = 0;
	std::vector<Core::Proc>& procs_;
	std::vector<Instruction> instructions_;
	Instruction* instr_;
};
