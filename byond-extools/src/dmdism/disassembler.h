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
	Disassembler(std::uint32_t* bc, unsigned int bc_len, std::vector<Core::Proc>& ps);
	Disassembly disassemble();

	~Disassembler() { delete context_; }

	Context* context() const { return context_; }

	bool disassemble_var(Instruction& instr);
	void add_call_args(Instruction& instr, unsigned int num_args);

private:
	Context* context_;
	
	Instruction disassemble_next();
};
