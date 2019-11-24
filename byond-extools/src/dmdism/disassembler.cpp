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

Disassembler::Disassembler(std::uint32_t* bc, unsigned int bc_len, std::vector<Core::Proc>& ps)
{
	std::vector<std::uint32_t> v(bc, bc + bc_len);
	context_ = new Context(v, ps);
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
	auto root = context_->eat();
	Instruction* instr = get_instr(root);
	/*auto cb = callbacks.find(static_cast<Bytecode>(root));
	if (cb != callbacks.end())
	{
		instr = cb->second();
	}
	else
	{
		instr = new Instr_UNK;
	}*/

	context_->set_instr(instr);
	instr->add_byte(root);

	instr->Disassemble(context_, this);

	return *instr;
}

bool Disassembler::disassemble_var(Instruction& instr)
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
		else if (val == PROC_)
		{
			instr.add_comment("." + Core::get_proc(context_->eat()).simple_name);
			add_call_args(instr, context_->eat());
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
		std::uint32_t type = context_->eat();
		std::uint32_t localno = context_->eat();

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
		context_->eat();
		instr.add_comment("CACHE");
		break;
	}
	case WORLD:
	case NULL_:
	case DOT:
	case SRC:
	{
		std::uint32_t type = context_->eat();

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
		context_->eat();
		instr.add_comment("ARGS");
		break;
	case PROC_NO_RET: //WAKE ME UP INSIDE
	case PROC_:
	{
		context_->eat();
		//std::uint32_t val = context_->eat();
		//Core::Alert("apin");
		//Core::Proc proc = Core::get_proc(val);
		//instr.add_comment(proc.name);
		break;
	}
	case SRC_PROC: //CAN'T WAKE UP
	case SRC_PROC_SPEC:
	{
		context_->eat();
		/*std::uint32_t val = context_->eat();
		//Core::Alert(std::to_string(val));
		std::string name = GetStringTableEntry(val)->stringData;
		std::replace(name.begin(), name.end(), ' ', '_');*/
		instr.add_comment("CACHE.");
		break;
	}
	default:
	{
		std::uint32_t val = context_->eat();
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
