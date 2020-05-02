#pragma once

#include "byond_functions.h"
#include "core.h"
#include "proc_management.h"
/*#ifdef _WIN32
#include <headers/CapstoneDisassembler.hpp>
#include <headers/Detour/x86Detour.hpp>
#else*/
#include "../third_party/subhook/subhook.h"
//#endif

typedef bool(*TopicFilter)(BSocket* socket, int socket_id);
extern TopicFilter current_topic_filter;

namespace Core
{
	struct Proc;
}

struct QueuedCall
{
	Core::Proc& proc;
	Value src;
	Value usr;
	std::vector<Value> args;
};

namespace Core
{
	void* untyped_install_hook(void* original, void* hook);

	// Used to ensure everything is the same function pointer type.
	template<typename FnPtr>
	FnPtr install_hook(FnPtr original, FnPtr hook)
	{
		return (FnPtr) untyped_install_hook((void*) original, (void*) hook);
	}

	void remove_hook(void* func);
	void remove_all_hooks();
	bool hook_custom_opcodes();
	void set_topic_filter(TopicFilter tf);
	//void schedule_call(Proc proc, std::vector<Value> args, Value src = Value::Null(), Value usr = Value::Null());
}