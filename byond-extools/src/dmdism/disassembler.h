#pragma once

#include "../core/core.h"
#include "../core/byond_structures.h"
#include "../core/proc_management.h"
#include "disassembly.h"
#include <vector>


#include "instruction.h"

class Context;

class Disassembler
{
public:
	Disassembler(std::vector<std::uint32_t> bc, std::vector<Core::Proc>& ps);
	Disassembly disassemble();

	Context* context() const { return context_; }

	void disassemble_var(Instruction& instr);
	void add_call_args(Instruction& instr, unsigned int num_args);

private:
	Context* context_;
	
	Instruction disassemble_next();
};
