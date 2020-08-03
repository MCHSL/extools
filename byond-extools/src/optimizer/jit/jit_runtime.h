#pragma once

#include "JitContext.h"
#include "DMCompiler.h"

using namespace dmjit;

trvh JitEntryPoint(void* code_base, const unsigned int args_len, Value* const args, const Value src, const Value usr, JitContext* ctx, bool was_suspended);
bool enable_jit();