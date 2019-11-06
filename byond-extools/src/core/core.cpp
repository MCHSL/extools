#include "core.h"
#include "find_functions.h"
#include "../tffi/tffi.h"
#include "../proxy/proxy_object.h"
#include "../optimizer/optimizer.h"
#include "../extended_profiling/extended_profiling.h"

CrashProcPtr CrashProc;
SuspendPtr Suspend;
StartTimingPtr StartTiming;
SetVariablePtr SetVariable;
GetVariablePtr GetVariable;
GetStringTableIndexPtr GetStringTableIndex;
GetStringTableIndexUTF8Ptr GetStringTableIndexUTF8;
GetProcArrayEntryPtr GetProcArrayEntry;
GetStringTableEntryPtr GetStringTableEntry;
CallGlobalProcPtr CallGlobalProc;
GetProfileInfoPtr GetProfileInfo;
GetByondVersionPtr GetByondVersion;
GetByondBuildPtr GetByondBuild;
ProcCleanupPtr ProcCleanup;
CreateContextPtr CreateContext;

ExecutionContext** Core::current_execution_context_ptr;
ExecutionContext** Core::parent_context_ptr_hack;
ProcSetupEntry** Core::proc_setup_table;

int ByondVersion;
int ByondBuild;
unsigned int* Core::some_flags_including_profile;

std::map<unsigned int, opcode_handler> Core::opcode_handlers;
std::map<std::string, unsigned int> Core::name_to_opcode;
unsigned int next_opcode_id = 0x1337;
bool Core::initialized = false;

bool Core::initialize()
{
	if (initialized)
	{
		return true;
	}
	initialized = find_functions() && populate_proc_list() && hook_custom_opcodes();
	return initialized;
}

void Core::Alert(std::string what) {
#ifdef _WIN32
	MessageBoxA(NULL, what.c_str(), "Ouch!", NULL);
#else
	printf("%s\n", what);
#endif
}

unsigned int Core::GetString(const char* str) {
	switch (ByondVersion) {
		case 512:
			return GetStringTableIndex(str, 0, 1);
		case 513:
			return GetStringTableIndexUTF8(str, 1, 0, 1);
		default: break;
	}
	return 0;
}

unsigned int Core::register_opcode(std::string name, opcode_handler handler)
{
	unsigned int next_opcode = next_opcode_id++;
	opcode_handlers[next_opcode] = handler;
	name_to_opcode[name] = next_opcode;
	return next_opcode;
}

ExecutionContext* Core::get_context()
{
	return *current_execution_context_ptr;
}

ExecutionContext* Core::_get_parent_context()
{
	return *parent_context_ptr_hack;
}

Value Core::get_stack_value(unsigned int which)
{
	return (*Core::current_execution_context_ptr)->stack[(*Core::current_execution_context_ptr)->stack_size - which - 1];
}

void Core::stack_pop(unsigned int how_many)
{
	(*Core::current_execution_context_ptr)->stack_size -= how_many;
}

void Core::stack_push(Value val)
{
	(*Core::current_execution_context_ptr)->stack_size++;
	(*Core::current_execution_context_ptr)->stack[(*Core::current_execution_context_ptr)->stack_size-1] = val;
}

void Core::enable_profiling()
{
	*some_flags_including_profile |= FLAG_PROFILE;
}

void Core::disable_profiling()
{
	*some_flags_including_profile &= ~FLAG_PROFILE;
}


const char* good = "gucci";
const char* bad = "pain";

extern "C" EXPORT const char* enable_profiling(int n_args, const char* args)
{
	Core::enable_profiling();
	return good;
}

extern "C" EXPORT const char* disable_profiling(int n_args, const char* args)
{
	Core::disable_profiling();
	return good;
}

extern "C" EXPORT const char* core_initialize(int n_args, const char* args)
{
	if (!Core::initialize())
	{
		Core::Alert("Core init failed!");
		return bad;
	}
	optimizer_initialize();
#ifdef _WIN32 // i ain't fixing this awful Linux situation anytime soon
	extended_profiling_initialize();
#endif
	return good;
}

extern "C" EXPORT const char* tffi_initialize(int n_args, const char* args)
{
	if (!(Core::initialize() && TFFI::initialize()))
		return bad;
	return good;
}

extern "C" EXPORT const char* proxy_initialize(int n_args, const char* args)
{
	if (!(Core::initialize() && Proxy::initialize()))
		return bad;
	return good;
}

void init_testing();
void run_tests();
extern "C" EXPORT const char* run_tests(int n_args, const char* args)
{
	init_testing();
	run_tests();
	return good;
}