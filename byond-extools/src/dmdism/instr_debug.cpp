#include "instr_debug.h"
#include "context.h"
#include "helpers.h"
#include "disassembler.h"

void Instr_DBG_FILE::Disassemble(Context* context, Disassembler* dism)
{
	const std::uint32_t file = context->eat(this);
	std::string filename = byond_tostring(file);
	comment_ = "Source file: \"" + filename + "\"";
	dism->debug_file(filename);
}

void Instr_DBG_LINENO::Disassemble(Context* context, Disassembler* dism)
{
	const std::uint32_t line = context->eat(this);
	comment_ = "Line number: " + todec(line);
	dism->debug_line(line);
}
