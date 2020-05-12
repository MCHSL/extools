

#include "instr_text.h"
#include "helpers.h"
#include "context.h"

void Instr_OUTPUT_FORMAT::Disassemble(Context* context, Disassembler* dism)
{
	std::uint32_t str_id = context->eat(this);
	context->eat(this);
	std::string woop = byond_tostring(str_id);
	comment_ = '"';
	int bruh = 0;
	for (char& c : woop)
	{
		if (c == -1)
		{
			comment_ += "[STACK" + std::to_string(bruh) + "]";
			bruh++;
		}
		else
		{
			comment_ += c;
		}
	}
	comment_ += '"';
}
