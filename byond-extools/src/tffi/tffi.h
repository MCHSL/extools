#pragma once
#include "../core/core.h"
#include <cmath>
#include <thread>
#ifndef _WIN32
#include <link.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

namespace TFFI
{
	bool initialize();
}