#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include "opcodes.h"
#include "opcode.h"

class Instruction
{
public:
	Instruction() {}
	Instruction(Bytecode op);
	Instruction(std::uint32_t op) : Instruction((Bytecode) op) {}

	std::size_t size() { return bytes_.size(); }

	std::vector<std::uint32_t>& bytes() { return bytes_; }
	void add_byte(std::uint32_t byte);
	std::string bytes_str();

	Opcode& opcode() { return opcode_; }

	std::string comment() { return comment_; }
	void set_comment(std::string comment) { comment_ = comment; }
	void add_comment(std::string comment) { comment_ += comment; }

	std::uint32_t offset() const { return offset_; }
	void set_offset(std::uint32_t offset) { offset_ = offset; }

	bool operator==(const Bytecode rhs);
	bool operator==(const std::uint32_t rhs);

	std::vector<unsigned short>& jump_locations() { return jump_locations_; }
	void add_jump(unsigned short off) { jump_locations_.push_back(off); }

	std::vector<std::string> extra_info() { return extra_info_; }
	void add_info(std::string s) { extra_info_.push_back(s); }

	std::pair<AccessModifier, unsigned int> acc_base; // If this instruction accesses variables, this will contain the necessary information.
	std::vector<unsigned int> acc_chain; // For example, 'world.a.b.c' results in acc_base of {AccessModifier::WORLD, 0} and acc_chain of {41, 42, 43} (example string IDs)
	
protected:
	Opcode opcode_;
	std::vector<std::uint32_t> bytes_;
	std::string comment_;
	std::uint32_t offset_;
	std::vector<std::string> extra_info_;

	std::vector<unsigned short> jump_locations_; //this is probably a sin but I don't feel like making a subtype of Instruction that supports jump destinations and then having to untangle the disassembler to make them work with all the other types.
};
