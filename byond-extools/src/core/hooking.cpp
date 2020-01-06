#include "hooking.h"
#include "../tffi/tffi.h"
#include <chrono>
#include <fstream>
#include "json.hpp"
#include <stack>
#include "../extended_profiling/extended_profiling.h"

CrashProcPtr oCrashProc;
CallGlobalProcPtr oCallGlobalProc;
TopicFloodCheckPtr oTopicFloodCheck;

TopicFilter current_topic_filter = nullptr;

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
	if(proc_id < Core::codecov_executed_procs.size())
		Core::codecov_executed_procs[proc_id] = true;
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

void hCrashProc(char *error, variadic_arg_hack hack) //this is a hack to pass variadic arguments to the original function, the struct contains a 1024 byte array
{
	int argument = *(int*)hack.data;
	if (Core::opcode_handlers.find(argument) != Core::opcode_handlers.end())
	{
		Core::opcode_handlers[argument](*Core::current_execution_context_ptr);
		return;
	}
	oCrashProc(error, hack);
}

bool hTopicFloodCheck(int socket_id)
{
	if (current_topic_filter)
	{
		return !current_topic_filter(GetBSocket(socket_id), socket_id); //inverting here cause it seems that the function is more like "CheckIsBanned" where it returns true if byond needs to ignore the client
	}
	return oTopicFloodCheck(socket_id);
}

void Core::set_topic_filter(TopicFilter tf)
{
	current_topic_filter = tf;
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
	oTopicFloodCheck = (TopicFloodCheckPtr)install_hook((void*)TopicFloodCheck, (void*)hTopicFloodCheck);
	return oCrashProc && oCallGlobalProc;
}