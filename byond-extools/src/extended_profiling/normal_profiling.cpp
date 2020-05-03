#include "normal_profiling.h"
#include "../core/core.h"
#include "../core/byond_structures.h"
#include <string>
#include "../dmdism/opcodes.h"
#include "../third_party/json.hpp"
#include <fstream>
#include <chrono>

trvh get_profile(unsigned int args_len, Value* args, Value src)
{
	std::string proc = Core::stringify(args[0]);
	Container res = List();
	ProfileInfo* profile = Core::get_proc(proc).profile();
	res["call count"] = (float)profile->call_count; // This proc has been called 8232093.000000000001 times
	res["real"] = profile->real.as_seconds();
	res["total"] = profile->total.as_seconds();
	res["self"] = profile->self .as_seconds();
	res["overtime"] = profile->overtime.as_seconds();
	return res;
}

trvh dump_all_profiles(unsigned int args_len, Value* args, Value src)
{
	auto start = std::chrono::high_resolution_clock::now();
	std::vector<nlohmann::json> interresult;
	for (const Core::Proc& p : Core::get_all_procs())
	{
		if (!p.raw_path.empty() && p.raw_path.back() == ')')
		{
			continue;
		}
		ProfileInfo* prof = p.profile();
		nlohmann::json j;
		j["proc"] = p.raw_path;
		j["call count"] = prof->call_count;
		if (prof->call_count)
		{
			j["real"] = prof->real.as_seconds();
			j["total"] = prof->total.as_seconds();
			j["self"] = prof->self.as_seconds();
			j["overtime"] = prof->overtime.as_seconds();
		}
		else
		{
			j["real"] = 0.0f;
			j["total"] = 0.0f;
			j["self"] = 0.0f;
			j["overtime"] = 0.0f;
		}

		interresult.push_back(j);
	}
	nlohmann::json jresult = interresult;
	std::string result = jresult.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	auto end = std::chrono::high_resolution_clock::now();
	unsigned long long milis = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	//Core::Alert(std::to_string(milis));
	std::ofstream out("profile_dump.txt");
	out << result;
	return Value::Null();
}

bool initialize_profiler_access()
{
	Core::enable_profiling();
	Core::get_proc("/proc/get_proc_profile").hook(get_profile);
	Core::get_proc("/proc/dump_all_profiles").hook(dump_all_profiles);
	return true;
}