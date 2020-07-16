#pragma once

#include "../../core/core.h"
#include "JitContext.h"

struct JitArguments
{
	uint32_t padding1;
	void* code_base;
	uint32_t padding2;
	dmjit::JitContext* jc;
};

void* compile_one(Core::Proc& proc);
trvh JitEntryPoint(void* code_base, unsigned int args_len, Value* args, Value src, Value usr, dmjit::JitContext* ctx);