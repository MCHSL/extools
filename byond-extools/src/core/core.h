#pragma once
#include "sigscan/sigscan.h"
#include "byond_structures.h"
#include "internal_functions.h"
#include "hooking.h"
#include "proc_management.h"
#ifndef _WIN32
#include <dlfcn.h>
#endif

#include <map>
#include <string>
#include <cmath>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

typedef void(*opcode_handler)(ExecutionContext* ctx);

extern int ByondVersion;
extern int ByondBuild;

namespace Core
{
	extern std::map<unsigned int, opcode_handler> opcode_handlers;
	extern std::map<std::string, unsigned int> name_to_opcode;
	extern ExecutionContext** current_execution_context_ptr;
	extern ExecutionContext** parent_context_ptr_hack;
	extern ProcSetupEntry** proc_setup_table;
	extern unsigned int* some_flags_including_profile;
	unsigned int GetStringId(std::string str);
	std::string GetStringFromId(unsigned int id);
	Value get_turf(int x, int y, int z);
	extern unsigned int extended_profiling_insanely_hacky_check_if_its_a_new_call_or_resume;
	ExecutionContext* get_context();
	ExecutionContext* _get_parent_context();
	unsigned int register_opcode(std::string name, opcode_handler handler);
	void Alert(std::string what);
	bool initialize();
	extern bool initialized;
	Value get_stack_value(unsigned int which);
	void stack_pop(unsigned int how_many);
	void stack_push(Value val);
	void enable_profiling();
	void disable_profiling();
	std::string type_to_text(unsigned int type);
}