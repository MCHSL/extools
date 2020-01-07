#include "maptick.h"
#include "../core/core.h"
#include <chrono>

SendMapsPtr oSendMaps;

void hSendMaps()
{
	auto start = std::chrono::high_resolution_clock::now();
	oSendMaps();
	auto end = std::chrono::high_resolution_clock::now();
	Core::global_direct_set("internal_tick_usage", std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 100000.0f);
}

bool enable_maptick()
{
	oSendMaps = (SendMapsPtr)Core::install_hook((void*)SendMaps, (void*)hSendMaps);
	return oSendMaps;
}