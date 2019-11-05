#include "extended_profiling.h"
#include "../core/hooking.h"
#include <unordered_map>
#include <fstream>
#include <algorithm>

ProcCleanupPtr oProcCleanup;
CreateContextPtr oCreateContext;
SuspendPtr oSuspend;

#ifdef _WIN32
static PLH::CapstoneDisassembler *disassembler;
PLH::x86Detour *CreateContextDetour;
PLH::x86Detour *ProcCleanupDetour;
#else
urmem::hook CreateContextDetour;
urmem::hook ProcCleanupDetour;
#endif

std::unordered_map<unsigned int, bool> procs_to_profile;

#define TOMICROS(x) (long long)std::chrono::duration_cast<std::chrono::microseconds>(x).count()
unsigned int next_profile_id = 1;

std::unordered_map<unsigned int, ExtendedProfile*> sleeping_profiles;
std::unordered_map<unsigned int, ExtendedProfile*> profiles_by_id;

void output_subcalls(std::string base, std::ofstream& output, ExtendedProfile* profile)
{
	base += Core::get_proc(profile->proc_id).name;
	//Core::Alert(base.c_str());

	if (!(profile->subcalls.empty()))
	{
		output << base << " " << TOMICROS(profile->subcalls.front()->start_time - profile->start_time) << "\n";
		for (ExtendedProfile* sub : profile->subcalls)
		{
			//Core::Alert( ("End: " + std::to_string(sub->end_time.time_since_epoch().count())).c_str());
			//Core::Alert(("Start: " +std::to_string(sub->start_time.time_since_epoch().count())).c_str());
			output_subcalls(base + ";", output, sub);
		}
		ExtendedProfile* last_sub = profile->subcalls.back();
		output << base << " " << TOMICROS(profile->end_time - last_sub->end_time) << "\n";
	}
	//base.pop_back();
	//Core::Alert(std::to_string(profile->end_time.time_since_epoch().count()).c_str());
	//Core::Alert(std::to_string(profile->start_time.time_since_epoch().count()).c_str());
	output << base << " " << TOMICROS(profile->end_time - profile->start_time) << "\n";
}

void dump_extended_profile(ExtendedProfile* profile)
{
	std::ofstream output("extended_profile.txt", std::fstream::app);
	output_subcalls("", output, profile);
	output.flush();
}

std::unordered_map<int, ExtendedProfile*> profiles;

void hCreateContext(ProcConstants* constants, ExecutionContext* new_context)
{
	oCreateContext(constants, new_context);
	new_context = Core::get_context();
	if (procs_to_profile.find(constants->proc_id) != procs_to_profile.end())
	{
		//Core::Alert("Creating profile for: " + Core::get_proc(constants->proc_id).name);
		int hash = new_context->hash();
		ExtendedProfile* profile = new ExtendedProfile;
		profile->proc_id = constants->proc_id;
		profile->hash = hash;
		profile->call_stack.push_back(profile);
		profiles[hash] = profile;
		profile->start_timer();
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
			if (ep->call_stack.size() < 15 && ep->call_stack.back()->proc_id == parent_context->constants->proc_id)
			{
				ExtendedProfile* subprofile = new ExtendedProfile;
				subprofile->proc_id = constants->proc_id;
				subprofile->call_stack.push_back(subprofile);
				ep->call_stack.back()->subcalls.push_back(subprofile);
				//ep->call_stack.back()->call_stack.push_back(subprofile);
				ep->call_stack.push_back(subprofile);
				subprofile->start_timer();
			}
		}
	}
}

void hProcCleanup(ExecutionContext* ctx)
{
	int hash = ctx->hash();
	int proc_id = ctx->constants->proc_id;
	if (profiles.find(hash) != profiles.end())
	{
		ExtendedProfile* profile = profiles[hash];
		profile->stop_timer();
		//Core::Alert("Destroying profile for: " + Core::get_proc(proc_id).name);
		if (profile->call_stack.back() != profile)
		{
			Core::Alert("Profile call stack not empty!");
		}
		dump_extended_profile(profile);
		for (ExtendedProfile* sub : profile->subcalls)
		{
			delete sub;
		}
		delete profile;
		profiles.erase(hash);
		oProcCleanup(ctx);
		return;
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

bool extended_profiling_initialize()
{
#ifdef _WIN32
	Hook* createContext = Core::install_hook(CreateContext, hCreateContext);
	if(!createContext)
		return false;
	oCreateContext = (CreateContextPtr)createContext->trampoline;
	CreateContextDetour = createContext->hook;
	Hook* procCleanup = Core::install_hook(ProcCleanup, hProcCleanup);
	if(!procCleanup)
		return false;
	oProcCleanup = (ProcCleanupPtr)procCleanup->trampoline;
	ProcCleanupDetour = procCleanup->hook;
	//oSuspend = (SuspendPtr)Core::install_hook(Suspend, hSuspend);
#else
	CreateContextDetour.install(urmem::get_func_addr(CreateContext), urmem::get_func_addr(hCreateContext));
	oCreateContext = (CreateContextPtr)CreateContextDetour.get_original_addr();
	ProcCleanupDetour.install(urmem::get_func_addr(ProcCleanup), urmem::get_func_addr(hProcCleanup));
	oProcCleanup = (ProcCleanupPtr)ProcCleanupDetour.get_original_addr();
#endif
	return oCreateContext && oProcCleanup;
}

void ExtendedProfile::start_timer()
{
	start_time = std::chrono::steady_clock::now();
}

void ExtendedProfile::stop_timer()
{
	end_time = std::chrono::steady_clock::now();
}

unsigned long long ExtendedProfile::total_time()
{
	return TOMICROS(end_time - start_time);
}