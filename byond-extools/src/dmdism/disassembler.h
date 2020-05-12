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
	bool disassemble_var_alt(Instruction& instr);
	void add_call_args(Instruction& instr, unsigned int num_args);

private:
	std::unique_ptr<Context> context_;

	Instruction disassemble_next();
};
