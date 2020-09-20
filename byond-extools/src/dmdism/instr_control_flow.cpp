#include "instr_custom.h"
#include "instruction.h"
#include "context.h"
#include "disassembler.h"
#include "helpers.h"

#include <algorithm>

void dis_custom_call(Instruction* instruction, Context* context, Disassembler* dism)
{
	instruction->set_comment("");
	if (!dism->disassemble_var(*instruction))
	{
		instruction->add_comment("CACHE.");
	}
	dism->disassemble_proc(*instruction);
}

void dis_custom_callglob(Instruction* instruction, Context* context, Disassembler* dism)
{
	std::uint32_t num_args = context->eat(instruction);
	std::uint32_t proc_id = context->eat(instruction);

	if (proc_id < context->procs().size())
	{
		instruction->add_comment(context->procs().at(proc_id).name);
	}
	else
	{
		instruction->add_comment("INVALID_PROC");
	}
	dism->add_call_args(*instruction, num_args);
}

void dis_custom_call_global_arglist(Instruction* instruction, Context* context, Disassembler* dism)
{
	std::uint32_t proc_id = context->eat(instruction);

	if (proc_id < context->procs().size())
	{
		instruction->add_comment(context->procs().at(proc_id).name);
	}
	else
	{
		instruction->add_comment("INVALID_PROC");
	}
}

void dis_custom_switch(Instruction* instruction, Context* context, Disassembler* dism)
{
	std::uint32_t case_count = context->eat(instruction);
	instruction->add_comment(std::to_string(case_count) + " cases, default jump to ");
	instruction->add_info("Cases");
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
			instruction->add_jump(jmp);
			instruction->add_info("NUMBER " + std::to_string(f.f) + " -> " + std::to_string(jmp));
			continue;
		}

		if (auto ptr = datatype_names.find(static_cast<DataType>(type)); ptr != datatype_names.end())
		{
			instruction->add_info(ptr->second + " ");
		}
		else
		{
			instruction->add_info("??? ");
		}
		std::uint32_t value = context->take();
		if (type == STRING)
		{
			instruction->add_info('"' + byond_tostring(value) + '"');
		}
		else
		{
			instruction->add_info(tohex(value));
		}
		instruction->add_info(" -> " + std::to_string(context->take()));
	}
	std::uint16_t default_jump = context->take();
	instruction->add_jump(default_jump);
	instruction->add_comment(std::to_string(default_jump));
}

void dis_custom_pick_switch(Instruction* instruction, Context* context, Disassembler* dism)
{
	std::uint32_t case_count = context->eat(instruction);
	instruction->add_comment(std::to_string(case_count) + " cases, default jump to ");
	instruction->add_info("Cases");
	for (int i = 0; i < case_count; i++)
	{
		std::uint32_t threshold = context->take();
		std::uint32_t jmp = context->take();
		instruction->add_jump(jmp);
		instruction->add_info("if <= " + std::to_string(threshold) + " -> " + std::to_string(jmp));
	}
	std::uint16_t default_jump = context->take();
	instruction->add_jump(default_jump);
	instruction->add_comment(std::to_string(default_jump));
}
