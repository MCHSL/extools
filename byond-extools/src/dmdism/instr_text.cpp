#include "instr_custom.h"
#include "instruction.h"
#include "context.h"
#include "disassembler.h"
#include "helpers.h"

void dis_custom_output_format(Instruction* instruction, Context* context, Disassembler* dism)
{
	std::uint32_t str_id = context->eat(instruction);
	context->eat(instruction);
	std::string woop = byond_tostring(str_id);
	instruction->set_comment("\"");
	int bruh = 0;
	for (char& c : woop)
	{
		if (c == -1)
		{
			instruction->add_comment("[STACK" + std::to_string(bruh) + "]");
			bruh++;
		}
		else
		{
			instruction->add_comment(std::string(1, c));
		}
	}
	instruction->add_comment("\"");
}
