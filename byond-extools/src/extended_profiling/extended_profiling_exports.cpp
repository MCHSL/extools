#include "../core/core.h"
#include "extended_profiling.h"
#include "memory_profiling.h"

// TODO: make this work on Linux. -steamport
extern "C" EXPORT const char* extended_profiling_initialize(int n_args, const char** args)
{
	if (!(Core::initialize() && actual_extended_profiling_initialize()))
		return Core::FAIL;
	return Core::SUCCESS;
}

extern "C" EXPORT const char* enable_extended_profiling(int n_args, const char** args)
{
	//Core::Alert("Enabling logging for " + std::string(args[0]));
	Core::get_proc(args[0]).extended_profile();
	return Core::SUCCESS;
}

extern "C" EXPORT const char* disable_extended_profiling(int n_args, const char** args)
{
	procs_to_profile.erase(Core::get_proc(args[0]).id); //TODO: improve consistency and reconsider how initialization works
	return Core::SUCCESS;
}

extern "C" EXPORT const char* dump_memory_usage(int n_args, const char** args)
{
	if (!Core::initialize())
		return Core::FAIL;

	dump_full_obj_mem_usage(args[0]);
	return Core::SUCCESS;
}