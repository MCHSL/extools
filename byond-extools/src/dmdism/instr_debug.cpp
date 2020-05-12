#include "instr_debug.h"
#include "context.h"
#include "helpers.h"

void Instr_DBG_FILE::Disassemble(Context* context, Disassembler* dism)
{
	const std::uint32_t file = context->eat(this);

	comment_ = "Source file: \"" + byond_tostring(file) + "\"";
}

void Instr_DBG_LINENO::Disassemble(Context* context, Disassembler* dism)
{
	const std::uint32_t line = context->eat(this);

	comment_ = "Line number: " + todec(line);
}
