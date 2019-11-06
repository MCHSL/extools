#pragma once

#include "internal_functions.h"
#include "core.h"
#include "proc_management.h"
#ifdef _WIN32
#include <headers/CapstoneDisassembler.hpp>
#include <headers/Detour/x86Detour.hpp>
#else
#include "urmem.hpp"
#endif

namespace Core
{
#ifdef _WIN32
	void* install_hook(void* original, void* hook);
#endif
	bool hook_custom_opcodes();
}