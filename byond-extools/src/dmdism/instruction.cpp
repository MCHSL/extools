#include "instruction.h"
#include "helpers.h"
#include "context.h"

Instruction::Instruction(Bytecode op)
	: opcode_ { op }
	, bytes_ { (std::uint32_t) op }
{
}

std::string Instruction::bytes_str()
{
	std::string result;
	for (std::uint32_t b : bytes_)
	{
		result.append(tohex(b));
		result.append(" ");
	}
	result.pop_back();

	return result;
}

void Instruction::add_byte(std::uint32_t byte)
{
	bytes_.emplace_back(byte);
}

bool Instruction::operator==(const Bytecode rhs)
{
	return opcode().opcode() == rhs;
}

bool Instruction::operator==(const std::uint32_t rhs)
{
	return rhs == opcode().opcode();
}
