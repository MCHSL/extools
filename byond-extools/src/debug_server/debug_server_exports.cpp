#include "../core/core.h"
#include "debug_server.h"

extern "C" EXPORT const char* debug_initialize(int n_args, const char** args)
{
	// Fallback values if called
	const char* mode = DBG_MODE_NONE;
	const char* port = DBG_DEFAULT_PORT;

	// Read from environment, set from shell or from DAP-controlled launch
	const char* env_mode = getenv("EXTOOLS_MODE");
	const char* env_port = getenv("EXTOOLS_PORT");
	if (env_mode)
	{
		mode = env_mode;
	}
	if (env_port)
	{
		port = env_port;
	}

	// Read from args, for maximum customizability
	if (n_args > 0 && *args[0])
	{
		mode = args[0];
	}
	if (n_args > 1 && *args[1])
	{
		port = args[1];
	}

	// Return early if debugging is not enabled by config.
	if (!strcmp(mode, DBG_MODE_NONE))
	{
		return "";
	}

	if (!Core::initialize()) {
		Core::Alert("Core init failed!");
		return Core::FAIL;
	}

	if (!debugger_initialize()) {
		Core::Alert("Debugger init failed!");
		return Core::FAIL;
	}

	if (!debugger_enable(mode, port)) {
		Core::Alert("Failed to enable debugger!");
		return Core::FAIL;
	}

	return Core::SUCCESS;
}

extern "C" EXPORT void force_debug()
{
	Core::initialize() && debugger_initialize() && debugger_enable(DBG_MODE_BACKGROUND, DBG_DEFAULT_PORT);
}
