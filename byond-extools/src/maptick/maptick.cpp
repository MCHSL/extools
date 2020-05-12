#include "maptick.h"
#include "../core/core.h"
#include <chrono>
#include "../datum_socket/datum_socket.h"

//#define MAPTICK_FAST_WRITE

SendMapsPtr oSendMaps;

void hSendMaps()
{
	auto start = std::chrono::high_resolution_clock::now();
	oSendMaps();
	auto end = std::chrono::high_resolution_clock::now();
#ifdef MAPTICK_FAST_WRITE
	Core::global_direct_set("internal_tick_usage", std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 100000.0f);
#else
	Value::Global().set("internal_tick_usage", std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 100000.0f);
#endif
}

bool enable_maptick()
{
	oSendMaps = Core::install_hook(SendMaps, hSendMaps);
	return oSendMaps;
}
