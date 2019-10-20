#include "core.h"
#include "find_functions.h"
#include "../tffi/tffi.h"

CrashProcPtr CrashProc;
SuspendPtr Suspend;
StartTimingPtr StartTiming;
SetVariablePtr SetVariable;
GetVariablePtr GetVariable;
GetStringTableIndexPtr GetStringTableIndex;
GetProcArrayEntryPtr GetProcArrayEntry;
GetStringTableEntryPtr GetStringTableEntry;

ExecutionContext** Core::current_execution_context_ptr;
ProcSetupEntry** Core::proc_setup_table;

std::map<unsigned int, opcode_handler> Core::opcode_handlers;
std::map<std::string, unsigned int> Core::name_to_opcode;
unsigned int next_opcode_id = 0x1337;

bool Core::initialize()
{
	return find_functions() && populate_proc_list();
}

void Core::Alert(const char* what) {
#ifdef _WIN32
	MessageBoxA(NULL, what, "Ouch!", NULL);
#else
	printf("%s\n", what);
#endif
}

unsigned int Core::register_opcode(std::string name, opcode_handler handler)
{
	unsigned int next_opcode = next_opcode_id++;
	opcode_handlers[next_opcode] = handler;
	name_to_opcode[name] = next_opcode;
	return next_opcode;
}

const char* good = "gucci";
const char* bad = "pain";

extern "C" EXPORT const char* core_initialize(int n_args, const char* args)
{
	if (!Core::initialize())
	{
		Core::Alert("Core init failed!");
		return bad;
	}
	if (!Core::hook_em())
	{
		Core::Alert("Hooking failed!");
		return bad;
	}
	return good;
}

extern "C" EXPORT const char* tffi_initialize(int n_args, const char* args)
{
	if (!TFFI::initialize())
		return bad;
	return good;
}