
#include <iostream>

#include "context.h"
#include "helpers.h"
#include "opcodes.h"


std::uint32_t Context::peek()
{
	if (current_offset_ >= buffer_.size())
	{
		std::cout << "READ PAST END OF BYTECODE" << std::endl;
		return RET;
	}
	return buffer_[current_offset_];
}

std::uint32_t Context::take()
{
	if (current_offset_ >= buffer_.size())
	{
		std::cout << "READ PAST END OF BYTECODE" << std::endl;
		return END;
	}

	return buffer_[current_offset_++];
}

std::uint32_t Context::eat(Instruction* instr)
{
	std::uint32_t op = take();
	if (instr != nullptr)
	{
		instr->add_byte(op);
	}

	return op;
}

std::uint32_t Context::eat_add(Instruction* instr)
{
	std::uint32_t val = eat(instr);
	if (instr != nullptr)
	{
		instr->opcode().add_info(" " + tohex(val));
	}

	return val;
}
