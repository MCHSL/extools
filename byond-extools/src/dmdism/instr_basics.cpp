#include "instr_custom.h"
#include "instruction.h"
#include "context.h"
#include "disassembler.h"
#include "helpers.h"

void dis_custom_pushval(Instruction* instr, Context* context, Disassembler* dism)
{
	std::uint32_t type = context->eat(instr);
	if (type == DataType::NUMBER)
	{
		typedef union
		{
			int i; float f;
		} funk;
		funk f;
		std::uint32_t first_part = context->eat(instr);
		std::uint32_t second_part = context->eat(instr);
		f.i = first_part << 16 | second_part; //32 bit floats are split into two 16 bit halves in the bytecode. Cool right?
		instr->opcode().add_info(" NUMBER " + tohex(f.i));
		instr->set_comment(std::to_string(f.f));
		return;
	}

	if (auto ptr = datatype_names.find(static_cast<DataType>(type)); ptr != datatype_names.end())
	{
		instr->opcode().add_info(" " + ptr->second);
	}
	else
	{
		instr->opcode().add_info(" ??? ");
	}
	std::uint32_t value = context->eat(instr);
	if (type == DataType::STRING)
	{
		instr->add_comment('"' + byond_tostring(value) + '"');
	}
}

void dis_custom_isinlist(Instruction* instr, Context* context, Disassembler* dism)
{
	std::uint32_t type = context->eat(instr);
	if(type == 0x0B)
	{
		//Checks if the argument is inside a range: if(x in 1 to 10)
		instr->opcode().add_info(" INRANGE");
		instr->add_comment("range");
		dism->add_call_args(*instr, 2);
	}
	//type 0x05 check if argument is in list, leaving as default unless more types show up
	else if(type != 0x05)
	{
		instr->opcode().add_info(" UNKNOWN ISINLIST ARGUMENT");
	}
}
