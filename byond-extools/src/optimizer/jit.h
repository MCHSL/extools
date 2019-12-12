#pragma once
#include "../core/proc_management.h"
#include "../dmdism/opcodes.h"

struct JitTrace
{
	Core::Proc proc;
	unsigned int call_count = 0;
	std::vector<DataType> arg_types;
};

extern std::unordered_map<unsigned int, JitTrace> traced_procs;
extern std::ofstream jit_out;

void consider_jit(Core::Proc p);
void reconsider_jit(JitTrace jt);
void record_jit(unsigned int proc_id, Value* args, unsigned int args_len);
void jit_compile(Core::Proc p);