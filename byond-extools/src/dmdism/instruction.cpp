


#include "instruction.h"
#include "helpers.h"
#include "context.h"


Instruction::Instruction(Bytecode op)
{
	opcode_ = Opcode(op);
}

Instruction::Instruction(unsigned int unknown_op)
{
	opcode_ = Opcode((Bytecode)unknown_op);
	add_byte(unknown_op);
}

Instruction Instruction::create(Bytecode op)
{
	Instruction instr;
	instr.opcode_ = Opcode(op);
	instr.add_byte(op);
	return instr;
}

Instruction Instruction::create(unsigned int op)
{
	return Instruction::create((Bytecode)op);
}

void Instruction::Disassemble(Context* context, Disassembler* dism)
{
	for (unsigned int i = 0; i < arguments(); i++)
	{
		std::uint32_t val = context->eat_add(this);
	}
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

bool Instruction::operator==(const unsigned int rhs)
{
	return opcode().opcode() == (Bytecode)rhs;
}

