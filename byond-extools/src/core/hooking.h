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
	bool hook_em();
}