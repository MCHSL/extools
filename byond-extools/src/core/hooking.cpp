#include "hooking.h"
#include "../tffi/tffi.h"
#include <chrono>
#include <fstream>
#include "json.hpp"
#include <stack>
#include "../extended_profiling/extended_profiling.h"

CrashProcPtr oCrashProc;
CallGlobalProcPtr oCallGlobalProc;

std::unordered_map<void*, subhook::Hook*> hooks;

//ExecutionContext* last_suspended_ec;

trvh REGPARM3 hCallGlobalProc(char usr_type, int usr_value, int proc_type, unsigned int proc_id, int const_0, char src_type, int src_value, Value *argList, unsigned int argListLen, int const_0_2, int const_0_3)
{
	Core::extended_profiling_insanely_hacky_check_if_its_a_new_call_or_resume = proc_id;
	if (proc_hooks.find((unsigned short)proc_id) != proc_hooks.end())
	{
		trvh result = proc_hooks[proc_id](argListLen, argList, src_type ? Value(src_type, src_value) : Value::Null());
		return result;
	}
	trvh result = oCallGlobalProc(usr_type, usr_value, proc_type, proc_id, const_0, src_type, src_value, argList, argListLen, const_0_2, const_0_3);
	Core::extended_profiling_insanely_hacky_check_if_its_a_new_call_or_resume = -1;
	return result;
}

void hCrashProc(char *error, int argument)
{
	if (Core::opcode_handlers.find(argument) != Core::opcode_handlers.end())
	{
		Core::opcode_handlers[argument](*Core::current_execution_context_ptr);
		return;
	}
	oCrashProc(error, argument);
}


void* Core::install_hook(void* original, void* hook)
{
	subhook::Hook* /*I am*/ shook = new subhook::Hook;
	shook->Install(original, hook);
	hooks[original] = shook;
	return shook->GetTrampoline();
}

void Core::remove_hook(void* func)
{
	hooks[func]->Remove();
	hooks.erase(func);
	delete hooks[func];
}


bool Core::hook_custom_opcodes() {
	oCrashProc = (CrashProcPtr)install_hook((void*)CrashProc, (void*)hCrashProc);
	oCallGlobalProc = (CallGlobalProcPtr)install_hook((void*)CallGlobalProc, (void*)hCallGlobalProc);
	return oCrashProc && oCallGlobalProc;
}