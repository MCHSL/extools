#pragma once
#include "../core/core.h"
#include <chrono>
#include <vector>
#include <unordered_map>
#include <stack>
#include <memory>

struct ExtendedProfile
{
	unsigned int proc_id;
	unsigned int id;
	unsigned long long total;
	std::vector<std::unique_ptr<ExtendedProfile>> subcalls;
	std::vector<ExtendedProfile*> call_stack;
	std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
	std::chrono::time_point<std::chrono::high_resolution_clock> end_time;

	void start_timer();
	void stop_timer();
	unsigned long long total_time();
};

extern std::unordered_map<unsigned int, bool> procs_to_profile;
bool actual_extended_profiling_initialize();