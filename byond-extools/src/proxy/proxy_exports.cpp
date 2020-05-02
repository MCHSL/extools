#include "../core/core.h"
#include "proxy_object.h"

extern "C" EXPORT const char* proxy_initialize(int n_args, const char** args)
{
	if (!(Core::initialize() && Proxy::initialize()))
		return Core::FAIL;
	return Core::SUCCESS;
}
