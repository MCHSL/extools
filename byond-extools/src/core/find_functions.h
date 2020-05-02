#pragma once
#include "byond_functions.h"
#include "sigscan/sigscan.h"
#include "core.h"
#ifdef _WIN32
#include <windows.h>
#endif

namespace Core
{
	bool find_functions();
	bool verify_compat();
}