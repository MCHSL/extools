#include "maptick.h"
#include "../core/core.h"
#include <chrono>

SendMapsPtr oSendMaps;

float last_duration = 0;

trvh sendmaps_duration(unsigned int argcount, Value* args, Value src)
{
	trvh t{ 0x2A };
	t.valuef = last_duration;
	return t;
}

void hSendMaps()
{
	auto start = std::chrono::high_resolution_clock::now();
	oSendMaps();
	auto end = std::chrono::high_resolution_clock::now();
	last_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 100000.0f;
}

bool enable_maptick()
{
	oSendMaps = (SendMapsPtr)Core::install_hook((void*)SendMaps, (void*)hSendMaps);
	Core::get_proc("/proc/get_sendmaps_elapsed_time").hook(sendmaps_duration);
	return oSendMaps;
}