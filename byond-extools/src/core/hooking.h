#pragma once

#include "internal_functions.h"
#include "core.h"
#ifdef _WIN32
#include <headers/CapstoneDisassembler.hpp>
#include <headers/Detour/x86Detour.hpp>
#else
#include "urmem.hpp"
#endif

namespace Core
{
	void* install_hook(void* original, void* hook);
	bool hook_custom_opcodes();
}