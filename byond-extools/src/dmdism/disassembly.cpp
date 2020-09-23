#include "disassembly.h"
#include "disassembler.h"

// Older GCC compilers don't have C++20 support yet.
#if __cplusplus > 201703L
using local_lower_bound = std::lower_bound;
#else
// "Second version" from https://en.cppreference.com/w/cpp/algorithm/lower_bound
template<class ForwardIt, class T, class Compare>
ForwardIt local_lower_bound(ForwardIt first, ForwardIt last, const T& value, Compare comp)
{
    ForwardIt it;
    typename std::iterator_traits<ForwardIt>::difference_type count, step;
    count = std::distance(first, last);

    while (count > 0) {
        it = first;
        step = count / 2;
        std::advance(it, step);
        if (comp(*it, value)) {
            first = ++it;
            count -= step + 1;
        }
        else
            count = step;
    }
    return first;
}
#endif

std::vector<std::uint32_t> Disassembly::assemble()
{
	std::vector<std::uint32_t> ret;
	for (Instruction i : instructions)
	{
		for (int op : i.bytes())
		{
			ret.push_back(op);
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
	auto it = local_lower_bound(instructions.begin(), instructions.end(), offset, [](Instruction const& instr, int offset) { return instr.offset() < offset; });

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
	std::size_t sum = 0;
	for (Instruction& i : instructions)
	{
		sum += i.size();
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

Disassembly Disassembly::from_proc(Core::Proc& proc)
{
	std::uint32_t* bytecode = proc.get_bytecode();
	Disassembly dis = Disassembler(std::vector<uint32_t>(bytecode, bytecode + proc.get_bytecode_length()), Core::get_all_procs()).disassemble();
	dis.proc = &proc;
	return dis;
}
