


#include <algorithm>

#include "instr_control_flow.h"
#include "helpers.h"
#include "context.h"
#include "disassembler.h"

//TODO: finish
void Instr_CALL::Disassemble(Context* context, Disassembler* dism)
{
	comment_ = "";
	if (dism->disassemble_var(*this))
	{
		return;
	}

	std::uint32_t procid = context->eat(this);
	std::string name = byond_tostring(procid);
	std::replace(name.begin(), name.end(), ' ', '_');
	comment_ += name;
	std::uint32_t num_args = context->eat(this);

	dism->add_call_args(*this, num_args);
}

void Instr_CALLNR::Disassemble(Context* context, Disassembler* dism)
{
	comment_ = "";
	if (dism->disassemble_var(*this))
	{
		return;
	}

	std::uint32_t procid = context->eat(this);
	std::string name = byond_tostring(procid);
	std::replace(name.begin(), name.end(), ' ', '_');
	comment_ += name;
	std::uint32_t num_args = context->eat(this);

	dism->add_call_args(*this, num_args);
}

void Instr_CALLGLOB::Disassemble(Context* context, Disassembler* dism)
{
	std::uint32_t num_args = context->eat(this);
	std::uint32_t proc_id = context->eat(this);

	if (proc_id < context->procs().size())
	{
		comment_ += context->procs().at(proc_id).name;
	}
	else
	{
		comment_ += "INVALID_PROC";
	}
	dism->add_call_args(*this, num_args);
}

void Instr_CALL_GLOBAL_ARGLIST::Disassemble(Context* context, Disassembler* dism)
{
	std::uint32_t proc_id = context->eat(this);

	if (proc_id < context->procs().size())
	{
		comment_ += context->procs().at(proc_id).name;
	}
	else
	{
		comment_ += "INVALID_PROC";
	}
}

void Instr_SWITCH::Disassemble(Context* context, Disassembler* dism)
{
	std::uint32_t case_count = context->eat(this);
	comment_ += std::to_string(case_count) + " cases, default jump to ";
	add_info("Cases");
	for (int i = 0; i < case_count; i++)
	{
		std::uint32_t type = context->take(); //TODO: Perhaps extract into a separate function to disassemble variables.
		if (type == NUMBER)
		{
			typedef union
			{
				int i; float f;
			} funk;
			funk f;
			std::uint32_t first_part = context->take();
			std::uint32_t second_part = context->take();
			f.i = first_part << 16 | second_part;
			std::uint16_t jmp = context->take();
			add_jump(jmp);
			add_info("NUMBER " + std::to_string(f.f) + " -> " + std::to_string(jmp));
			continue;
		}

		if (datatype_names.find(static_cast<DataType>(type)) != datatype_names.end())
		{
			add_info(datatype_names.at(static_cast<DataType>(type)) + " ");
		}
		else
		{
			add_info("??? ");
		}
		std::uint32_t value = context->take();
		if (type == STRING)
		{
			add_info('"' + byond_tostring(value) + '"');
		}
		else
		{
			add_info(tohex(value));
		}
		add_info(" -> " + std::to_string(context->take()));
	}
	std::uint16_t default_jump = context->take();
	add_jump(default_jump);
	comment_ += std::to_string(default_jump);
}