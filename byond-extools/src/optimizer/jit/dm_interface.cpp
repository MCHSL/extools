#include "DMCompiler.h"
#include "JitContext.h"

#include "../../core/core.h"
#include "../../dmdism/instruction.h"
#include "../../core/core.h"

#include "analysis.h"
#include "jit_runtime.h"


using namespace asmjit;
using namespace dmjit;

extern "C" EXPORT const char* jit_test(int n_args, const char** args)
{
	if (!Core::initialize())
	{
		return Core::FAIL;
	}

	enable_jit();
	//compile({&Core::get_proc("/proc/jit_test_compiled_proc"), &Core::get_proc("/proc/recursleep")});
	//Core::get_proc("/datum/subtype/proc/buttfart").jit();
	//Core::get_proc("/proc/tiny_proc").jit();
	Core::get_proc("/proc/jit_test_compiled_proc").jit();
	Core::get_proc("/proc/jit_wrapper").set_bytecode({ Bytecode::RET, Bytecode::RET, Bytecode::RET });
	return Core::SUCCESS;
}

extern "C" EXPORT const char* jit_compile(int n_args, const char** args)
{
	if (!Core::initialize())
	{
		return Core::FAIL;
	}

	static bool res_hooked = false;
	if(!res_hooked)
	{
		enable_jit();
		res_hooked = true;
	}

	Core::get_proc(args[0]).jit();
	return Core::SUCCESS;
}