#define NOMINMAX
#include <iostream>
#include <functional>
#include <algorithm>

#include "disassembler.h"
#include "opcodes.h"
#include "helpers.h"
#include "callbacks.h"
#include "context.h"


Disassembler::Disassembler(std::vector<std::uint32_t> bc, const std::vector<Core::Proc>& ps)
{
	context_ = std::make_unique<Context>(bc, ps);
}

Disassembler::Disassembler(std::uint32_t* bc, unsigned int bc_len, const std::vector<Core::Proc>& ps)
{
	std::vector<std::uint32_t> v(bc, bc + bc_len);
	context_ = std::make_unique<Context>(v, ps);
}

Disassembly Disassembler::disassemble()
{
	std::vector<Instruction> instrs;
	instrs.reserve(512);
	while (context_->more()) {
		instrs.push_back(disassemble_next());
	}

	return Disassembly(std::move(instrs));
}

Instruction Disassembler::disassemble_next()
{
	auto offset = context_->current_offset();
	auto root = context_->eat(nullptr);
	std::unique_ptr<Instruction> instr = get_instr(root);
	/*auto cb = callbacks.find(static_cast<Bytecode>(root));
	if (cb != callbacks.end())
	{
		instr = cb->second();
	}
	else
	{
		instr = new Instr_UNK;
	}*/

	instr->set_offset(offset);
	instr->add_byte(root);
	instr->Disassemble(context(), this);

	return *instr;
}

bool Disassembler::disassemble_var_alt(Instruction& instr)
{
	std::uint32_t accessor = context_->eat(&instr);
	switch (accessor)
	{
	case LOCAL:
	case GLOBAL:
	case ARG:
	{
		std::uint32_t id = context_->eat(&instr);
		std::string modifier_name = modifier_names.at(static_cast<AccessModifier>(accessor));

		instr.opcode().add_info(" " + modifier_name + std::to_string(id));
		instr.add_comment(modifier_name + std::to_string(id));
		break;
	}
	}
	return false;
}

bool Disassembler::disassemble_var(Instruction& instr)
{
	switch (context_->peek())
	{
	case SUBVAR:
	{
		std::uint32_t val = context_->eat(&instr);
		instr.opcode().add_info(" SUBVAR");
		if (disassemble_var(instr))
		{
			return true;
		}

		val = context_->eat(&instr);
		if (val == SUBVAR)
		{
			if (disassemble_var(instr))
			{
				return true;
			}
		}
		else if (val == PROC_)
		{
			instr.add_comment("." + Core::get_proc(context_->eat(&instr)).simple_name);
			add_call_args(instr, context_->eat(&instr));
			return true;
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
	//case CACHE:
	case ARG:
	{
		std::uint32_t type = context_->eat(&instr);
		std::uint32_t localno = context_->eat(&instr);

		std::string modifier_name = "UNKNOWN_MODIFIER";
		if (modifier_names.find(static_cast<AccessModifier>(type)) != modifier_names.end())
		{
			modifier_name = modifier_names.at(static_cast<AccessModifier>(type));
		}

		instr.opcode().add_info(" " + modifier_name + std::to_string(localno));
		instr.add_comment(modifier_name + std::to_string(localno));
		break;
	}
	case CACHE:
	{
		context_->eat(&instr);
		instr.add_comment("CACHE");
		break;
	}
	case WORLD:
	case NULL_:
	case DOT:
	case SRC:
	{
		std::uint32_t type = context_->eat(&instr);

		std::string modifier_name = "UNKNOWN_MODIFIER";
		if (modifier_names.find(static_cast<AccessModifier>(type)) != modifier_names.end())
		{
			modifier_name = modifier_names.at(static_cast<AccessModifier>(type));
		}

		instr.opcode().add_info(" " + modifier_name);
		instr.add_comment(modifier_name);
		break;
	}
	case ARGS:
		context_->eat(&instr);
		instr.add_comment("ARGS");
		break;
	case PROC_NO_RET: //WAKE ME UP INSIDE
	case PROC_:
	{
		context_->eat(&instr);
		instr.add_comment("CACHE." + Core::get_proc(context_->eat(&instr)).simple_name);
		add_call_args(instr, context_->eat(&instr));
		return true;
	}
	case SRC_PROC: //CAN'T WAKE UP
	case SRC_PROC_SPEC:
	{
		context_->eat(&instr);
		/*std::uint32_t val = context_->eat();
		//Core::Alert(std::to_string(val));
		std::string name = GetStringTableEntry(val)->stringData;
		std::replace(name.begin(), name.end(), ' ', '_');*/
		instr.add_comment("CACHE.");
		break;
	}
	default:
	{
		std::uint32_t val = context_->eat(&instr);
		instr.add_comment("CACHE."+byond_tostring(val));
		break;
	}
	}
	return false;
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
