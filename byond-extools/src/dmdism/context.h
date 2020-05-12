#pragma once

#include <vector>
#include <unordered_map>

#include "../core/proc_management.h"
#include "instruction.h"

class Context
{
public:
	Context(std::vector<std::uint32_t> bc, const std::vector<Core::Proc>& ps) : buffer_(bc), procs_(ps) {}
	std::vector<std::uint32_t> buffer() const { return buffer_; }
	const std::vector<Core::Proc>& procs() const { return procs_; }
	bool more() const { return current_offset_ < buffer_.size(); }

	std::uint32_t peek();
	std::uint32_t take();
	std::uint32_t eat(Instruction* instr);
	std::uint32_t eat_add(Instruction* instr);

	std::uint32_t current_offset() const { return current_offset_; };

private:
	std::vector<std::uint32_t> buffer_;
	std::uint32_t current_offset_ = 0;
	const std::vector<Core::Proc>& procs_;
};
