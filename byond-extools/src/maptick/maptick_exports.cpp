#include "../core/core.h"
#include "maptick.h"

extern "C" EXPORT const char* maptick_initialize(int n_args, const char** args)
{
	if (!(Core::initialize() && enable_maptick()))
	{
		return Core::FAIL;
	}
	return Core::SUCCESS;
}
