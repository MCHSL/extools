#include "extended_profiling.h"
#include "../core/hooking.h"
#include <unordered_map>
#include <fstream>
#include <algorithm>
#ifdef WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#define STUPID_READOUTS_LIMIT 40000000

CreateContextPtr oCreateContext;
ProcCleanupPtr oProcCleanup;
SuspendPtr oSuspend;

std::unordered_map<unsigned int, bool> procs_to_profile;

#define TOMICROS(x) (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(x).count()
unsigned int next_profile_id = 1;

std::unordered_map<unsigned int, std::unique_ptr<ExtendedProfile>> sleeping_profiles;

unsigned long long output_subcalls(std::string base, std::ofstream& output, ExtendedProfile* profile)
{
	base += Core::get_proc(profile->proc_id).name;
	//Core::Alert(base.c_str());

	unsigned long long total_time = 0;
	if (!profile->subcalls.empty())
	{
		unsigned long long time = TOMICROS(profile->subcalls.front()->start_time - profile->start_time);
		total_time += time;
		if (time > STUPID_READOUTS_LIMIT)
		{
			time = 1;
		}
		if (time)
		{
			output << base << " " << time << "\n";
		}

		for (int i=0; i < profile->subcalls.size()-1; i++)
		{
			ExtendedProfile* sub = profile->subcalls.at(i).get();
			//Core::Alert( ("End: " + std::to_string(sub->end_time.time_since_epoch().count())).c_str());
			//Core::Alert(("Start: " +std::to_string(sub->start_time.time_since_epoch().count())).c_str());
			total_time += output_subcalls(base + ";", output, sub);
			total_time += TOMICROS(profile->subcalls.at(i + 1)->start_time - sub->end_time);
			output << base << " " << TOMICROS(profile->subcalls.at(i+1)->start_time - sub->end_time) << "\n";

		}
		ExtendedProfile* last_sub = profile->subcalls.back().get();
		output_subcalls(base + ";", output, last_sub);
		time = TOMICROS(profile->end_time - last_sub->end_time);
		total_time += time;
		if (time > STUPID_READOUTS_LIMIT)
		{
			time = 1;
		}
		if (time)
		{
			output << base << " " << time << "\n";
		}
		//unsigned long long drift = TOMICROS(profile->end_time - profile->start_time) - total_time;
		//output << base << ";OVERHEAD " << drift << "\n";
	}
	else
	{
		unsigned long long time = TOMICROS(profile->end_time - profile->start_time);
		total_time += time;
		//if (time > STUPID_READOUTS_LIMIT)
		//{
		//	time = 1;
		//}
		if (time)
		{
			output << base << " " << time << "\n";
		}
	}
	//base.pop_back();
	//Core::Alert(std::to_string(profile->end_time.time_since_epoch().count()).c_str());
	//Core::Alert(std::to_string(profile->start_time.time_since_epoch().count()).c_str());
	return total_time;
}

void dump_extended_profile(ExtendedProfile* profile)
{
	//Core::Alert(std::to_string(TOMICROS(real_end - real_start)));
	//Core::Alert(std::to_string(TOMICROS(profile->end_time - profile->start_time)));
	std::string procname = Core::get_proc(profile->proc_id).name;
	std::replace(procname.begin(), procname.end(), '/', '.');
#ifdef _WIN32
	CreateDirectoryA("profiling", NULL);
#else
	mkdir("profiling", 777);
#endif
	std::string filename = "./profiling/extended_profile" + procname + "." + std::to_string(profile->id) + ".txt";
	std::ofstream output(filename, std::fstream::app);
	if (!output.is_open())
	{
		Core::Alert("Failed to open file!");
		return;
	}
	output_subcalls("", output, profile);
	output.flush();
}

std::unordered_map<int, std::unique_ptr<ExtendedProfile>> profiles;

void recursive_start(ExtendedProfile* profile)
{
	for (const auto& subcall : profile->subcalls)
	{
		subcall->start_timer();
		recursive_start(subcall.get());
	}
}

void hCreateContext(ProcConstants* constants, ExecutionContext* new_context)
{
	//Core::Alert("HELLO?");
	oCreateContext(constants, new_context);
	new_context = Core::get_context();

	auto sleepy_iter = sleeping_profiles.find(constants->proc_id);

	if (profiles.find(constants->proc_id) != profiles.end() || (Core::extended_profiling_insanely_hacky_check_if_its_a_new_call_or_resume == constants->proc_id && sleepy_iter != sleeping_profiles.end()))
	{
		//Core::Alert("Profile already exists for " + Core::get_proc(constants->proc_id).name);
		return;
	}

	if (sleepy_iter != sleeping_profiles.end())
	{
		//Core::Alert("Reusing sleeping profile");
		/*ExtendedProfile* profile = sleeping_profiles[constants->proc_id];
		profile->hash = hash;
		profiles[hash] = profile;
		sleeping_profiles.erase(constants->proc_id);
		profile->start_timer();
		recursive_start(profile);*/
		std::unique_ptr<ExtendedProfile> sleepy = std::move(sleepy_iter->second);
		sleeping_profiles.erase(constants->proc_id);

		auto profile = std::make_unique<ExtendedProfile>();
		auto profile2 = profile.get();
		profile->proc_id = constants->proc_id;
		profile->id = sleepy->id;
		profile->call_stack.reserve(32);
		profile->call_stack.push_back(profile.get());
		profiles[constants->proc_id] = std::move(profile);
		sleepy.reset();
		profile2->start_timer();
		return;
	}
	else if (auto procs_to_profile_iter = procs_to_profile.find(constants->proc_id); procs_to_profile_iter != procs_to_profile.end())
	{
		//Core::Alert("Creating profile for: " + Core::get_proc(constants->proc_id).name);
		auto profile = std::make_unique<ExtendedProfile>();
		auto profile2 = profile.get();
		profile->proc_id = constants->proc_id;
		profile->id = next_profile_id++;
		profile->call_stack.push_back(profile.get());
		profiles[constants->proc_id] = std::move(profile);
		profile2->start_timer();
		procs_to_profile.erase(procs_to_profile_iter);
	}
	else
	{
		ExecutionContext* parent_context = new_context->parent_context;
		if (!parent_context)
		{
			parent_context = Core::_get_parent_context();
			if (!parent_context)
			{
				Core::Alert("Could not get parent context");
				return;
			}
		}
		for (const auto& p : profiles)
		{
			ExtendedProfile* ep = p.second.get();
			if (ep->call_stack.size() < 50 && ep->call_stack.back()->proc_id == parent_context->constants->proc_id)
			{
				std::unique_ptr<ExtendedProfile> subprofile = std::make_unique<ExtendedProfile>();
				ExtendedProfile* subprofile2 = subprofile.get();
				subprofile->proc_id = constants->proc_id;
				subprofile->call_stack.reserve(32);
				subprofile->call_stack.push_back(subprofile.get());
				ep->call_stack.back()->subcalls.push_back(std::move(subprofile));
				//ep->call_stack.back()->call_stack.push_back(subprofile);
				ep->call_stack.push_back(subprofile2);
				subprofile->start_timer();
			}
		}
	}
}


void recursive_suspend(ExtendedProfile* profile)
{
	for (const auto& subcall : profile->subcalls)
	{
		subcall->stop_timer();
		recursive_suspend(subcall.get());
	}
}

void hProcCleanup(ExecutionContext* ctx)
{
	int proc_id = ctx->constants->proc_id;
	if (sleeping_profiles.find(proc_id) == sleeping_profiles.end())
	{
		if (auto profiles_iter = profiles.find(proc_id); profiles_iter != profiles.end())
		{
			std::unique_ptr<ExtendedProfile> profile = std::move(profiles_iter->second);
			profiles.erase(profiles_iter);

			profile->stop_timer();
			//Core::Alert("Destroying profile for: " + Core::get_proc(proc_id).name);
			if (profile->call_stack.back() != profile.get())
			{
				Core::Alert("Profile call stack not empty!");
				do
				{
					ExtendedProfile* subprofile = profile->call_stack.back();
					subprofile->stop_timer();
					profile->call_stack.pop_back();
				} while (profile->call_stack.back() != profile.get());
			}
			//recursive_suspend(profile);
			dump_extended_profile(profile.get());
			profile.reset();
			//procs_to_profile.erase(proc_id);
			oProcCleanup(ctx);
			procs_to_profile[proc_id] = true;
			return;
		}
	}
	else
	{
		//Core::Alert("Found in sleeping procs, skipped profile destruction");
	}

	for (const auto& p : profiles)
	{
		ExtendedProfile* ep = p.second.get();
		//Core::Alert("Want: " + std::to_string(proc_id));
		//Core::Alert("Have: " + std::to_string(ep->call_stack.back()->proc_id));
		if (ep->call_stack.back()->proc_id == proc_id)
		{
			ExtendedProfile* subprofile = ep->call_stack.back();
			subprofile->stop_timer();
			ep->call_stack.pop_back();
		}
	}
	oProcCleanup(ctx);
}

SuspendedProc* hSuspend(ExecutionContext* ctx, int unknown)
{
	int proc_id = ctx->constants->proc_id;
	auto profile_iter = profiles.find(proc_id);
	if (/*procs_to_profile.find(proc_id) != procs_to_profile.end() || */profile_iter != profiles.end())
	{
		//profiles[proc_id]->stop_timer();
		//Core::Alert("Marking as sleepy");
		std::unique_ptr<ExtendedProfile> profile = std::move(profile_iter->second);
		profiles.erase(profile_iter);

		profile->stop_timer();
		//recursive_suspend(profile);
		dump_extended_profile(profile.get());
		sleeping_profiles[proc_id] = std::move(profile);
	}
	return oSuspend(ctx, unknown);
}

bool actual_extended_profiling_initialize()
{
#ifdef _WIN32
	oCreateContext = Core::install_hook(CreateContext, hCreateContext);
	oProcCleanup = Core::install_hook(ProcCleanup, hProcCleanup);
	oSuspend = Core::install_hook(Suspend, hSuspend);
	return oCreateContext && oProcCleanup && oSuspend;
#else
	Core::alert_dd("The extools extended profiler is not supported on Linux.");
	return false;
#endif
}

void ExtendedProfile::start_timer()
{
	start_time = std::chrono::high_resolution_clock::now();
}

void ExtendedProfile::stop_timer()
{
	end_time = std::chrono::high_resolution_clock::now();
}

unsigned long long ExtendedProfile::total_time()
{
	return TOMICROS(end_time - start_time);
}
