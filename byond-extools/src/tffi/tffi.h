#pragma once
#include "../core/core.h"
#include <thread>
#ifndef _WIN32
#include <link.h>
#include <unistd.h>
#endif

namespace TFFI
{
	bool initialize();
}