#include "instr_basics.h"
#include "helpers.h"
#include "context.h"



void Instr_PUSHVAL::Disassemble(Context* context, Disassembler* dism)
{
	std::uint32_t type = context->eat(this);
	if (type == DataType::NUMBER)
	{
		typedef union
		{
			int i; float f;
		} funk;
		funk f;
		std::uint32_t first_part = context->eat(this);
		std::uint32_t second_part = context->eat(this);
		f.i = first_part << 16 | second_part; //32 bit floats are split into two 16 bit halves in the bytecode. Cool right?
		opcode().add_info(" NUMBER " + tohex(f.i));
		comment_ = std::to_string(f.f);
		return;
	}

	if (datatype_names.find(static_cast<DataType>(type)) != datatype_names.end())
	{
		opcode().add_info(" " + datatype_names.at(static_cast<DataType>(type)));
	}
	else
	{
		opcode().add_info(" ??? ");
	}
	std::uint32_t value = context->eat(this);
	if (type == DataType::STRING)
	{
		comment_ += '"' + byond_tostring(value) + '"';
	}
}

void Instr_ISINLIST::Disassemble(Context* context, Disassembler* dism)
{
	std::uint32_t type = context->eat(this);
	if(type == 0x0B)
	{
		//Checks if the argument is inside a range: if(x in 1 to 10)
		opcode().add_info(" INRANGE");
		comment_ = "range";
		dism->add_call_args(*this, 2);
	}
	//type 0x05 check if argument is in list, leaving as default unless more types show up
	else if(type != 0x05)
	{
		opcode().add_info(" UNKNOWN ISINLIST ARGUMENT");
	}
}
