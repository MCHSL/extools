#pragma once
#include "../core/core.h"
#include <cmath>
#include <thread>
#ifndef _WIN32
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>
#endif

namespace TFFI
{
	bool initialize();
}

void cheap_hypotenuse(ExecutionContext* ctx);