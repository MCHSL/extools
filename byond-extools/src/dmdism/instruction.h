#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include "opcodes.h"
#include "opcode.h"

class Context;
class Disassembler;

class Instruction
{
public:
	Instruction() : Instruction(UNK) {}
	Instruction(Bytecode op);
	Instruction(unsigned int unknown_op);

	static Instruction create(Bytecode op);
	static Instruction create(unsigned int unknown_op);


	virtual void Disassemble(Context* context, Disassembler* dism);
	virtual unsigned int arguments() const { return 0; }


	std::uint8_t& size() { return size_; }

	std::vector<std::uint32_t>& bytes() { return bytes_; }
	void add_byte(std::uint32_t byte);
	std::string bytes_str();

	Opcode& opcode() { return opcode_; }

	std::string comment() { return comment_; }
	void set_comment(std::string comment) { comment_ = comment; }
	void add_comment(std::string comment) { comment_ += comment; }

	std::uint16_t offset() const { return offset_; }
	void set_offset(std::uint16_t offset) { offset_ = offset; }

	bool operator==(const Bytecode rhs);
	bool operator==(const unsigned int rhs);

	std::vector<unsigned short>& jump_locations() { return jump_locations_; }
	void add_jump(unsigned short off) { jump_locations_.push_back(off); }

	std::vector<std::string> extra_info() { return extra_info_; }
	void add_info(std::string s) { extra_info_.push_back(s); }
protected:
	std::uint8_t size_;
	std::vector<std::uint32_t> bytes_;
	Opcode opcode_;
	std::string comment_;
	std::uint32_t offset_;
	std::vector<std::string> extra_info_;

	std::vector<unsigned short> jump_locations_; //this is probably a sin but I don't feel like making a subtype of Instruction that supports jump destinations and then having to untangle the disassembler to make them work with all the other types.
};


class Instr_UNK : public Instruction
{
public:
	void Disassemble(Context* context, Disassembler* dism) override
	{
		comment_ = "Unknown instruction";
	}
};

#define ADD_INSTR(op)						\
	class Instr_##op : public Instruction	\
{											\
	using Instruction::Instruction;			\
};

#define ADD_INSTR_ARG(op, arg)								\
	class Instr_##op : public Instruction					\
{															\
	using Instruction::Instruction;							\
	unsigned int arguments() const override { return arg; }		\
};

#define ADD_INSTR_CUSTOM(op)												\
	class Instr_##op : public Instruction									\
{																			\
	using Instruction::Instruction;											\
	void Disassemble(Context* context, Disassembler* dism) override;		\
};

#define ADD_INSTR_VAR(op)																				\
	class Instr_##op : public Instruction																\
{																										\
	using Instruction::Instruction;																		\
	void Disassemble(Context* context, Disassembler* dism) override { dism->disassemble_var(*this); }	\
};

#define ADD_INSTR_JUMP(op, argcount)																		\
	class Instr_##op : public Instruction																\
{																										\
	using Instruction::Instruction;																		\
	void Disassemble(Context* context, Disassembler* dism) override { add_jump(context->eat(this)); for (unsigned int i = 1; i < arguments(); i++) std::uint32_t val = context->eat_add(this); }	\
	unsigned int arguments() const override { return argcount; }												\
};
//absolutely barbaric