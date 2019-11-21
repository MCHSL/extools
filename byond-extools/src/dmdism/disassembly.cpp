#include "disassembly.h"
#include "disassembler.h"

std::vector<std::uint32_t>* Disassembly::assemble()	{
	std::vector<std::uint32_t>* ret = new std::vector<std::uint32_t>();
	for (Instruction i : instructions)
	{
		for (int op : i.bytes())
		{
			ret->push_back(op);
		}
	}
	return ret;
}

Instruction& Disassembly::at(std::size_t i)
{
	return instructions.at(i);
}

Instruction* Disassembly::next_from_offset(std::uint16_t offset)
{
	auto it = std::lower_bound(instructions.begin(), instructions.end(), offset, [](Instruction const& instr, int offset) { return instr.offset() < offset; });

	if (it == instructions.end() || ++it == instructions.end())
	{
		Core::Alert("Could not find next instruction for offset"); //Ummm...
		return nullptr; //There. Sorry.
	}

	return &*it; //yes

}

std::vector<Instruction>::iterator Disassembly::begin() noexcept
{
	return instructions.begin();
}

std::vector<Instruction>::iterator Disassembly::end() noexcept
{
	return instructions.end();
}

std::uint32_t Disassembly::op_at(std::size_t i)
{
	return instructions.at(i).bytes().at(0);
}

void Disassembly::insert_at(std::size_t at, Instruction instr)
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

std::size_t Disassembly::bytecount()
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
	std::uint32_t* bytecode = proc.get_bytecode();
	return Disassembler(std::vector<uint32_t>(bytecode, bytecode + proc.get_bytecode_length()), procs_by_id).disassemble();
}

