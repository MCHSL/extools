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

#ifdef _WIN32
struct Hook {
	PLH::x86Detour* hook;
	void* trampoline;
};
#endif

namespace Core
{
#ifdef _WIN32
	Hook* install_hook(void* original, void* hook);
#endif
	bool hook_custom_opcodes();
}