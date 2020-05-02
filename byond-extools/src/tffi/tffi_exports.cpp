#include "../core/core.h"
#include "tffi.h"

extern "C" EXPORT const char* tffi_initialize(int n_args, const char** args)
{
	if (!(Core::initialize() && TFFI::initialize()))
		return Core::FAIL;
	return Core::SUCCESS;
}
