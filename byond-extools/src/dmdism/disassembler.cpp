#define NOMINMAX
#include <iostream>
#include <functional>
#include <algorithm>

#include "disassembler.h"
#include "opcodes.h"
#include "helpers.h"
#include "callbacks.h"
#include "context.h"


Disassembler::Disassembler(std::vector<std::uint32_t> bc, std::vector<Core::Proc>& ps)
{
	context_ = new Context(bc, ps);
}

Disassembly Disassembler::disassemble()
{
	std::vector<Instruction> instrs;
	while (context_->more()) {
		instrs.push_back(disassemble_next());
	}

	return Disassembly(instrs);
}

Instruction Disassembler::disassemble_next()
{
	Instruction* instr;

	auto root = context_->eat();
	std::function<Instruction*()> callback = nullptr;

	try
	{
		callback = callbacks.at(static_cast<Bytecode>(root));
	}
	catch (std::exception&)
	{
		
	}

	if (callback != nullptr)
	{
		instr = callback();
	}
	else
	{
		instr = new Instr_UNK;
	}

	context_->set_instr(instr);
	instr->add_byte(root);

	instr->Disassemble(context_, this);

	return *instr;
}

void Disassembler::disassemble_var(Instruction& instr)
{
	switch (context_->peek())
	{
	case SUBVAR:
	{
		std::uint32_t val = context_->eat();
		instr.opcode().add_info(" SUBVAR");
		disassemble_var(instr);

		val = context_->eat();
		if (val == SUBVAR)
		{
			disassemble_var(instr);
		}
		else
		{
			instr.opcode().add_info(" " + std::to_string(val));
			instr.add_comment("." + byond_tostring(val));
		}

		break;
	}

	case LOCAL:
	case GLOBAL:
	case CACHE:
	case ARG:
	{
		std::uint32_t type = context_->eat();
		std::uint32_t localno = context_->eat();

		instr.opcode().add_info(" " + modifier_names.at(static_cast<AccessModifier>(type)) + std::to_string(localno));
		instr.add_comment(modifier_names.at(static_cast<AccessModifier>(type)) + std::to_string(localno));
		break;
	}
	case WORLD:
	case NULL_:
	case DOT:
	case SRC:
	{
		std::uint32_t type = context_->eat();

		instr.opcode().add_info(" " + modifier_names.at(static_cast<AccessModifier>(type)));
		instr.add_comment(modifier_names.at(static_cast<AccessModifier>(type)));
		break;
	}
	default:
	{
		std::uint32_t val = context_->eat();
		instr.add_comment("SRC."+byond_tostring(val));
		break;
	}
	}
}

void Disassembler::add_call_args(Instruction& instr, unsigned int num_args)
{
	num_args = std::min((int)num_args, 16);
	instr.add_comment("(");
	for (unsigned int i = 0; i < num_args; i++)
	{
		instr.add_comment("STACK" + std::to_string(i) + ", ");
	}

	if (num_args) {
		instr.set_comment(instr.comment().substr(0, instr.comment().size() - 2));
	}

	instr.add_comment(")");
}
