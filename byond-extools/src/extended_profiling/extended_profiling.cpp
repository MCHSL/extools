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

std::unordered_map<unsigned int, ExtendedProfile*> sleeping_profiles;

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
			ExtendedProfile* sub = profile->subcalls.at(i);
			//Core::Alert( ("End: " + std::to_string(sub->end_time.time_since_epoch().count())).c_str());
			//Core::Alert(("Start: " +std::to_string(sub->start_time.time_since_epoch().count())).c_str());
			total_time += output_subcalls(base + ";", output, sub);
			total_time += TOMICROS(profile->subcalls.at(i + 1)->start_time - sub->end_time);
			output << base << " " << TOMICROS(profile->subcalls.at(i+1)->start_time - sub->end_time) << "\n";

		}
		ExtendedProfile* last_sub = profile->subcalls.back();
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
	std::string procname = Core::get_proc(profile->proc_id);
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

std::unordered_map<int, ExtendedProfile*> profiles;

void recursive_start(ExtendedProfile* profile)
{
	for (ExtendedProfile* subcall : profile->subcalls)
	{
		subcall->start_timer();
		recursive_start(subcall);
	}
}

void hCreateContext(ProcConstants* constants, ExecutionContext* new_context)
{
	//Core::Alert("HELLO?");
	oCreateContext(constants, new_context);
	new_context = Core::get_context();
	if (profiles.find(constants->proc_id) != profiles.end() || (Core::extended_profiling_insanely_hacky_check_if_its_a_new_call_or_resume == constants->proc_id && sleeping_profiles.find(constants->proc_id) != sleeping_profiles.end()))
	{
		//Core::Alert("Profile already exists for " + Core::get_proc(constants->proc_id).name);
		return;
	}
	if (procs_to_profile.find(constants->proc_id) != procs_to_profile.end() || sleeping_profiles.find(constants->proc_id) != sleeping_profiles.end())
	{
		//Core::Alert("Creating profile for: " + Core::get_proc(constants->proc_id).name);
		if (sleeping_profiles.find(constants->proc_id) != sleeping_profiles.end())
		{
			//Core::Alert("Reusing sleeping profile");
			/*ExtendedProfile* profile = sleeping_profiles[constants->proc_id];
			profile->hash = hash;
			profiles[hash] = profile;
			sleeping_profiles.erase(constants->proc_id);
			profile->start_timer();
			recursive_start(profile);*/
			ExtendedProfile* sleepy = sleeping_profiles[constants->proc_id];
			ExtendedProfile* profile = new ExtendedProfile;
			profile->proc_id = constants->proc_id;
			profile->id = sleepy->id;
			profile->call_stack.reserve(32);
			profile->call_stack.push_back(profile);
			profiles[constants->proc_id] = profile;
			delete sleepy;
			sleeping_profiles.erase(constants->proc_id);
			profile->start_timer();
			return;
		}
		ExtendedProfile* profile = new ExtendedProfile;
		profile->proc_id = constants->proc_id;
		profile->id = next_profile_id++;
		profile->call_stack.push_back(profile);
		profiles[constants->proc_id] = profile;
		profile->start_timer();
		procs_to_profile.erase(constants->proc_id);
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
		for (std::pair<const int, ExtendedProfile*>& p : profiles)
		{
			ExtendedProfile* ep = p.second;
			if (ep->call_stack.size() < 50 && ep->call_stack.back()->proc_id == parent_context->constants->proc_id)
			{
				ExtendedProfile* subprofile = new ExtendedProfile;
				subprofile->proc_id = constants->proc_id;
				subprofile->call_stack.reserve(32);
				subprofile->call_stack.push_back(subprofile);
				ep->call_stack.back()->subcalls.push_back(subprofile);
				//ep->call_stack.back()->call_stack.push_back(subprofile);
				ep->call_stack.push_back(subprofile);
				subprofile->start_timer();
			}
		}
	}
}


void recursive_suspend(ExtendedProfile* profile)
{
	for (ExtendedProfile* subcall : profile->subcalls)
	{
		subcall->stop_timer();
		recursive_suspend(subcall);
	}
}

void hProcCleanup(ExecutionContext* ctx)
{
	int proc_id = ctx->constants->proc_id;
	if (sleeping_profiles.find(proc_id) == sleeping_profiles.end())
	{
		if (profiles.find(proc_id) != profiles.end())
		{
			ExtendedProfile* profile = profiles[proc_id];
			profile->stop_timer();
			//Core::Alert("Destroying profile for: " + Core::get_proc(proc_id).name);
			if (profile->call_stack.back() != profile)
			{
				Core::Alert("Profile call stack not empty!");
				do
				{
					ExtendedProfile* subprofile = profile->call_stack.back();
					subprofile->stop_timer();
					profile->call_stack.pop_back();
				} while (profile->call_stack.back() != profile);
			}
			//recursive_suspend(profile);
			dump_extended_profile(profile);
			delete profile;
			profiles.erase(proc_id);
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

	for (std::pair<const int, ExtendedProfile*>& p : profiles)
	{
		ExtendedProfile* ep = p.second;
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
	if (/*procs_to_profile.find(proc_id) != procs_to_profile.end() || */profiles.find(proc_id) != profiles.end())
	{
		//profiles[proc_id]->stop_timer();
		//Core::Alert("Marking as sleepy");
		ExtendedProfile* profile = profiles[proc_id];
		sleeping_profiles[proc_id] = profile;
		profile->stop_timer();
		//recursive_suspend(profile);
		dump_extended_profile(profile);
		profiles.erase(proc_id);
	}
	return oSuspend(ctx, unknown);
}

bool actual_extended_profiling_initialize()
{
	oCreateContext = (CreateContextPtr)Core::install_hook((void*)CreateContext, (void*)hCreateContext);
	oProcCleanup = (ProcCleanupPtr)Core::install_hook((void*)ProcCleanup, (void*)hProcCleanup);
	oSuspend = (SuspendPtr)Core::install_hook((void*)Suspend, (void*)hSuspend);
	return oCreateContext && oProcCleanup && oSuspend;
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

ExtendedProfile::~ExtendedProfile()
{
	for (auto subcall : subcalls)
	{
		delete subcall;
	}
}