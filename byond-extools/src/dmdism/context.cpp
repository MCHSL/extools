
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

std::uint32_t Context::eat()
{
	if (current_offset_ >= buffer_.size())
	{
		std::cout << "READ PAST END OF BYTECODE" << std::endl;
		return RET;
	}

	last_opcode_ = buffer_[current_offset_++];
	if (instr_ != nullptr)
	{
		instr_->add_byte(last_opcode_);
	}

	return last_opcode_;
}

std::uint32_t Context::eat_add()
{
	std::uint32_t val = eat();
	if (instr_ != nullptr)
	{
		instr_->opcode().add_info(" " + tohex(last_opcode_));
	}

	return val;
}

void Context::set_instr(Instruction* instr)
{
	instr->set_offset(current_offset_ - 1);
	instr_ = instr;
}

void Context::finish_instr()
{
	instr_ = nullptr;
}
