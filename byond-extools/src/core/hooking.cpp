#include "hooking.h"
#include "../tffi/tffi.h"
#include <chrono>
#include <fstream>
#include "json.hpp"
#include <stack>
#include "../extended_profiling/extended_profiling.h"
#include "../optimizer/jit.h"
#include "../dmdism/opcodes.h"

CrashProcPtr oCrashProc;
CallGlobalProcPtr oCallGlobalProc;

std::unordered_map<void*, subhook::Hook*> hooks;

//ExecutionContext* last_suspended_ec;

struct QueuedCall
{
	Core::Proc proc;
	Value src;
	Value usr;
	std::vector<Value> args;
};

std::vector<QueuedCall> queued_calls;
bool calling_queue = false;

trvh REGPARM3 hCallGlobalProc(char usr_type, int usr_value, int proc_type, unsigned int proc_id, int const_0, char src_type, int src_value, Value *argList, unsigned int argListLen, int const_0_2, int const_0_3)
{
	codecov_executed_procs[proc_id] = true;
	if (!queued_calls.empty() && !calling_queue)
	{
		calling_queue = true;
		while(!queued_calls.empty())
		{
			auto qc = queued_calls.back();
			queued_calls.pop_back();
			qc.proc.call(qc.args, qc.usr, qc.src);
		}
		calling_queue = false;
	}
	if (traced_procs.find(proc_id) != traced_procs.end())
	{
		record_jit(proc_id, argList, argListLen);
	}
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

void spam()
{
	Sleep(30000);
	Core::Proc p = Core::get_proc("/atom/movable/proc/forceMove");
	std::vector<Value> args = { {0x0E, 0x00} };
	while (true)
	{
		args[0] = Core::get_turf(rand() % 255 + 1, rand() % 255 + 1, 2);
		QueuedCall q{ p, Value(0x02, rand()%0xFFF), Value::Null(), args };
		queued_calls.push_back(q);
		Sleep(100);
	}
}


bool Core::hook_custom_opcodes() {
	oCrashProc = (CrashProcPtr)install_hook((void*)CrashProc, (void*)hCrashProc);
	oCallGlobalProc = (CallGlobalProcPtr)install_hook((void*)CallGlobalProc, (void*)hCallGlobalProc);
	//std::thread(spam).detach();
	return oCrashProc && oCallGlobalProc;
}