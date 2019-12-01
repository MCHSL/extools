#pragma once
#include "../core/core.h"
#include <cmath>
#include <thread>
#ifndef _WIN32
#include <link.h>
#include <unistd.h>
#else
#include <Windows.h>
#endif

namespace TFFI
{
	bool initialize();
}

void cheap_hypotenuse(ExecutionContext* ctx);