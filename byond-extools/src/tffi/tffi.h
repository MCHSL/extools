#pragma once
#include "../core/core.h"
#include <cmath>
#include <thread>
#ifndef _WIN32
#include <link.h>
#include <unistd.h>
#endif

typedef const char* (byond_ffi_func)(int, const char**);
namespace TFFI
{
	extern std::uint32_t result_string_id;
	extern std::uint32_t completed_string_id;
	extern std::uint32_t internal_id_string_id ;
	extern std::map<float, SuspendedProc*> suspended_procs;
	extern std::map<std::string, std::map<std::string, byond_ffi_func*>> library_cache;
	bool initialize();
}

void cheap_hypotenuse(ExecutionContext* ctx);