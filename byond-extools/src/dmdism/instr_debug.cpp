#include "instr_custom.h"
#include "instruction.h"
#include "context.h"
#include "disassembler.h"
#include "helpers.h"

void dis_custom_dbg_file(Instruction* instruction, Context* context, Disassembler* dism)
{
	const std::uint32_t file = context->eat(instruction);
	std::string filename = byond_tostring(file);
	instruction->set_comment("Source file: \"" + filename + "\"");
	dism->debug_file(filename);
}

void dis_custom_dbg_lineno(Instruction* instruction, Context* context, Disassembler* dism)
{
	const std::uint32_t line = context->eat(instruction);
	instruction->set_comment("Line number: " + todec(line));
	dism->debug_line(line);
}
