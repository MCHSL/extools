#include "disassembly.h"
#include "disassembler.h"

std::vector<int>* Disassembly::assemble()	{
	std::vector<int>* ret = new std::vector<int>();
	for (Instruction i : instructions)
	{
		for (int op : i.bytes())
		{
			ret->push_back(op);
		}
	}
	return ret;
}

Instruction& Disassembly::at(unsigned int i)
{
	return instructions.at(i);
}

std::vector<Instruction>::iterator Disassembly::begin() noexcept
{
	return instructions.begin();
}

std::vector<Instruction>::iterator Disassembly::end() noexcept
{
	return instructions.end();
}

int Disassembly::op_at(unsigned int i)
{
	return instructions.at(i).bytes().at(0);
}

void Disassembly::insert_at(unsigned int at, Instruction instr)
{
	instructions.insert(instructions.begin() + at, instr);
}

void Disassembly::add_byte_to_last(unsigned int byte)
{
	instructions.back().add_byte(byte);
}

std::size_t Disassembly::size()
{
	return instructions.size();
}

int Disassembly::bytecount()
{
	int sum = 0;
	for (Instruction i : instructions)
	{
		sum += i.bytes().size();
	}
	return sum;
}

void Disassembly::recalculate_offsets()
{
	int current_offset = instructions.front().bytes().size();
	for (int i = 1; i < instructions.size(); i++)
	{
		Instruction& instr = instructions.at(i);
		instr.set_offset(current_offset);
		current_offset += instr.bytes().size();
	}
}

Disassembly Core::get_disassembly(std::string _proc)
{
	Core::Proc proc = Core::get_proc(_proc);
	int* bytecode = proc.get_bytecode();
	return Disassembler(std::vector<uint32_t>(bytecode, bytecode + proc.get_bytecode_length()), procs_by_id).disassemble();
}

