#include "hooking.h"
#include "../tffi/tffi.h"
#include <chrono>
#include <fstream>
#include "../third_party/json.hpp"
#include <stack>
#include "../extended_profiling/extended_profiling.h"
#include <mutex>
#include "../optimizer/jit/Test.h"

CrashProcPtr oCrashProc;
CallGlobalProcPtr oCallGlobalProc;
TopicFloodCheckPtr oTopicFloodCheck;
StartTimingPtr oStartTiming;

TopicFilter current_topic_filter = nullptr;

std::unordered_map<void*, std::unique_ptr<subhook::Hook>> hooks;

//ExecutionContext* last_suspended_ec;

std::vector<QueuedCall> queued_calls;
bool calling_queue = false;

trvh REGPARM3 hCallGlobalProc(char usr_type, int usr_value, int proc_type, unsigned int proc_id, int const_0, DataType src_type, int src_value, Value *argList, unsigned char argListLen, int const_0_2, int const_0_3)
{
	//if(proc_id < Core::codecov_executed_procs.size())
	//	Core::codecov_executed_procs[proc_id] = true;
	/*if (!queued_calls.empty() && !calling_queue)
	{
		calling_queue = true;
		while(!queued_calls.empty())
		{
			auto qc = queued_calls.back();
			queued_calls.pop_back();
			if (qc.src)
			{
				qc.src.invoke(qc.proc.simple_name, qc.args, qc.usr);
			}
			else
			{
				qc.proc.call(qc.args, qc.usr);
			}
		}
		calling_queue = false;
	}*/

	auto jit_hooks_it = jit_hooks.find((unsigned short)proc_id);
	if (jit_hooks_it != jit_hooks.end())
	{
		// The first two Values passed as args to the jit wrapper contain the function code and the jit context.
		// This is necessary to have the ability to suspend and resume.
		// The code and context need to be extracted before passing the arguments to JitEntryPoint.
		Value* args_with_jit_stuff = new Value[argListLen + 2];
		args_with_jit_stuff[0].value = (int)jit_hooks_it->second;
		args_with_jit_stuff[1].value = (int)new dmjit::JitContext();
		std::copy(argList, argList + argListLen, args_with_jit_stuff + 2);
		auto result = CallGlobalProc(0, 0, 2, Core::get_proc("/proc/jit_wrapper").id, 0, DataType::NULL_D, 0, args_with_jit_stuff, argListLen + 2, 0, 0);
		// We can delete this because BYOND creates a copy of the ProcConstants struct when it will need one later, and copies over the arguments.
		delete[] args_with_jit_stuff;
		for (int i = 0; i < argListLen; i++)
		{
			DecRefCount(argList[i].type, argList[i].value);
		}
		return result;
	}

	Core::extended_profiling_insanely_hacky_check_if_its_a_new_call_or_resume = proc_id;
	if (proc_hooks.find((unsigned short)proc_id) != proc_hooks.end())
	{
		trvh result = proc_hooks[proc_id](argListLen, argList, src_type ? Value(src_type, src_value) : static_cast<Value>(Value::Null()));
		for (int i = 0; i < argListLen; i++)
		{
			DecRefCount(argList[i].type, argList[i].value);
		}
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

std::recursive_mutex timing_mutex;

#ifndef _WIN32
REGPARM3
#endif
void hStartTiming(ProcConstants* sp)
{
	std::lock_guard<std::recursive_mutex> lk(timing_mutex);
	oStartTiming(sp);
}

void Core::set_topic_filter(TopicFilter tf)
{
	current_topic_filter = tf;
}


void* Core::untyped_install_hook(void* original, void* hook)
{
	std::unique_ptr<subhook::Hook> /*I am*/ shook = std::make_unique<subhook::Hook>();
	shook->Install(original, hook);
	auto trampoline = shook->GetTrampoline();
	hooks[original] = std::move(shook);
	return trampoline;
}

void Core::remove_hook(void* func)
{
	hooks[func]->Remove();
	hooks.erase(func);
}

void Core::remove_all_hooks()
{
	for (auto iter = hooks.begin(); iter != hooks.end(); )
	{
		iter->second->Remove();
		iter = hooks.erase(iter);
	}
}

bool Core::hook_custom_opcodes() {
	oCrashProc = install_hook(CrashProc, hCrashProc);
	oCallGlobalProc = install_hook(CallGlobalProc, hCallGlobalProc);
	oTopicFloodCheck = install_hook(TopicFloodCheck, hTopicFloodCheck);
	oStartTiming = install_hook(StartTiming, hStartTiming);
	if (!(oCrashProc && oCallGlobalProc && oTopicFloodCheck && oStartTiming)) {
		Core::Alert("Failed to install hooks!");
		return false;
	}
	return true;
}
