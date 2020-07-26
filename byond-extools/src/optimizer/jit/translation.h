#pragma once
#pragma once
#include "../../core/core.h"
#include "../../third_party/robin_hood.h"

struct JittedInfo
{
	void* code_base;
	bool needs_sleep;

	JittedInfo(void* cb, bool ns) : code_base(cb), needs_sleep(ns) {}
	JittedInfo() : code_base(nullptr), needs_sleep(true) {} //by default assume that a proc needs sleep
};

extern robin_hood::unordered_map<unsigned int, JittedInfo> jitted_procs;

inline void add_jitted_proc(unsigned int proc_id, void* code_base)
{
	jitted_procs[proc_id] = JittedInfo(code_base, true);
}
