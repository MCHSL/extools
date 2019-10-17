#pragma once

#include "internal_functions.h"
#include "core.h"
#include "../polyhook/headers/CapstoneDisassembler.hpp"
#include "../polyhook/headers/Detour/x86Detour.hpp"

namespace Core
{
	bool hook_em();
}