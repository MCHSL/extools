#include "../core/core.h"
#include "reftracking.h"

extern "C" EXPORT const char* ref_tracking_initialize(int n_args, const char** args)
{
	if (!(Core::initialize() && RefTracking::initialize()))
		return Core::FAIL;
	return Core::SUCCESS;
}
