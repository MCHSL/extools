#pragma once

#include "../core/core.h"
#include "../core/byond_structures.h"
#include "../core/proc_management.h"
#include "disassembly.h"
#include <vector>
#include <memory>


#include "instruction.h"
#include "context.h"


class Disassembler
{
public:
	Disassembler(std::vector<std::uint32_t> bc, const std::vector<Core::Proc>& ps);
	Disassembler(std::uint32_t* bc, unsigned int bc_len, const std::vector<Core::Proc>& ps);
	Disassembly disassemble();

	Context* context() const { return context_.get(); }

	bool disassemble_var(Instruction& instr);
	std::vector<unsigned int> disassemble_subvar_follows(Instruction& instr);
	bool disassemble_proc(Instruction& instr);
	void add_call_args(Instruction& instr, unsigned int num_args);

	void debug_file(std::string file) { last_file = file; }
	void debug_line(uint32_t line) { last_line = line; }

private:
	std::unique_ptr<Context> context_;
	std::string last_file;
	uint32_t last_line = 0;

	Instruction disassemble_next();
};
