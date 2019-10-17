#pragma once
#include "sigscan/sigscan.h"
#include "byond_structures.h"
#include "internal_functions.h"
#include "hooking.h"
#include "proc_management.h"

#include <map>
#include <string>

typedef void(*opcode_handler)(ExecutionContext* ctx);

namespace Core
{
	extern std::map<unsigned int, opcode_handler> opcode_handlers;
	extern std::map<std::string, unsigned int> name_to_opcode;
	extern ExecutionContext** current_execution_context_ptr;
	extern ProcSetupEntry** proc_setup_table;
	unsigned int register_opcode(std::string name, opcode_handler handler);

	bool initialize();
}