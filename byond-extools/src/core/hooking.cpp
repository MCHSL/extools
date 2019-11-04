#include "hooking.h"
#include "../tffi/tffi.h"
#include <chrono>
#include <fstream>
#include "json.hpp"
#include <stack>

#ifdef _WIN32
static PLH::CapstoneDisassembler* disassembler;
#else
urmem::hook CrashProcDetour;
#endif
CrashProcPtr oCrashProc;
CallGlobalProcPtr oCallGlobalProc;

#define TOMICROS(x) std::chrono::duration_cast<std::chrono::microseconds>(x).count()

struct ExtendedProfile
{
	unsigned int proc_id;
	int* bytecode; //facilitate faster searches up the call stack
	std::vector<ExtendedProfile*> subcalls;
	std::chrono::time_point<std::chrono::steady_clock> start_time;
	std::chrono::time_point<std::chrono::steady_clock> end_time;
	unsigned long long total_time = 0;
	int call_id = 0;
};

ExtendedProfile* proc_being_profiled;
ExtendedProfile* profile_result;
std::stack<ExtendedProfile*> call_stack;

void output_subcalls(std::string base, std::ofstream& output, ExtendedProfile* profile, int depth)
{
	base += Core::get_proc(profile->proc_id).name+";";
	//Core::Alert(base.c_str());
	if (!(profile->subcalls.empty()))
	{
		output << base << "self " << TOMICROS(profile->subcalls.back()->start_time - profile->start_time) << "\n";
		for (ExtendedProfile* sub : profile->subcalls)
		{
			output_subcalls(base, output, sub, ++depth);
		}
		ExtendedProfile* last_sub = profile->subcalls.back();
		output << base << "self " << TOMICROS(profile_result->end_time - last_sub->end_time) << "\n";
	}
	else
	{
		base.pop_back();
		output << base << " "<< TOMICROS(profile->end_time - profile->start_time) << "\n";
	}
}

void dump_extended_profile()
{
	/*nlohmann::json result;
	result["version"] = "0.0.1";
	result["$schema"] = "https://www.speedscope.app/file-format-schema.json";
	result["name"] = Core::get_proc(profile_result->proc_id).name;
	result["activeProfileIndex"] = 1;

	nlohmann::json shared;
	std::vector<std::pair<std::string, std::string>> frames;
	frames.push_back({ "name", "self" });
	frames.push_back({"name", Core::get_proc(profile_result->proc_id).name });
	for (ExtendedProfile* sub : profile_result->subcalls)
	{
		frames.push_back({ "name", Core::get_proc(sub->proc_id).name });
	}
	shared["frames"] = frames;
	result["shared"] = shared;

	nlohmann::json profile;
	profile["type"] = "sampled";
	profile["name"] = Core::get_proc(profile_result->proc_id).name;
	profile["unit"] = "microseconds";
	profile["startValue"] = 0;
	profile["endValue"] = profile_result->total_time;
	

	std::ofstream output("extended_profile.txt");
	output << result.dump();*/
	std::ofstream output("extended_profile.txt");
	output_subcalls("", output, profile_result, 0);
	output.flush();
}

trvh __cdecl hCallGlobalProc(char unk1, int unk2, int proc_type, unsigned int proc_id, int const_0, char unk3, int unk4, Value* argList, unsigned int argListLen, int const_0_2, int const_0_3)
{
	int call_id = 0;
	if (extended_profiling_procs.find(proc_id) != extended_profiling_procs.end())
	{
		proc_being_profiled = new ExtendedProfile();
		proc_being_profiled->proc_id = proc_id;
		proc_being_profiled->bytecode = Core::get_context()->bytecode;
		proc_being_profiled->start_time = std::chrono::high_resolution_clock::now();
		call_stack.push(proc_being_profiled);
	}
	if (proc_being_profiled && proc_being_profiled->proc_id != proc_id)
	{
		call_id = rand();
		ExecutionContext* parent_ctx = Core::_get_parent_context();
		if (call_stack.top()->proc_id == Core::get_proc(parent_ctx->bytecode).id)
		{
			ExtendedProfile* subcall = new ExtendedProfile();
			subcall->proc_id = proc_id;
			subcall->bytecode = Core::get_proc(proc_id).get_bytecode();
			subcall->start_time = std::chrono::high_resolution_clock::now();
			subcall->call_id = call_id;
			call_stack.top()->subcalls.push_back(subcall);
			call_stack.push(subcall);
		}
	}
	if (proc_hooks.find(proc_id) != proc_hooks.end())
	{
		trvh result = proc_hooks[proc_id](argList, argListLen);
		Core::Alert("hook");
		return result;
	}
	trvh result = oCallGlobalProc(unk1, unk2, proc_type, proc_id, const_0, unk3, unk4, argList, argListLen, const_0_2, const_0_3);
	if (proc_being_profiled)
	{
		ExtendedProfile* top = call_stack.top();
		if (top->proc_id == proc_id)
		{
			ExtendedProfile* sub = call_stack.top();
			sub->end_time = std::chrono::high_resolution_clock::now();
			call_stack.pop();
			sub->total_time = std::chrono::duration_cast<std::chrono::microseconds>(sub->end_time - sub->start_time).count();
		}
		if (proc_being_profiled->proc_id == proc_id)
		{
			proc_being_profiled->end_time = std::chrono::high_resolution_clock::now();
			proc_being_profiled->total_time = std::chrono::duration_cast<std::chrono::microseconds>(proc_being_profiled->end_time - proc_being_profiled->start_time).count();
			profile_result = proc_being_profiled;
			proc_being_profiled = nullptr;
			dump_extended_profile();
			//Core::Alert("Extended profile results written to file");
		}
	}
	return result;
}

void hCrashProc(char* error, int argument)
{
	if (Core::opcode_handlers.find(argument) != Core::opcode_handlers.end())
	{
		Core::opcode_handlers[argument](*Core::current_execution_context_ptr);
		return;
	}
#ifdef _WIN32
	oCrashProc(error, argument);
#else
	CrashProcDetour.call(error, argument);
#endif
}


void* Core::install_hook(void* original, void* hook)
{
#ifdef _WIN32
	disassembler = new PLH::CapstoneDisassembler(PLH::Mode::x86);
	if (!disassembler)
	{
		return nullptr;
	}
	std::uint64_t trampoline;
	PLH::x86Detour* detour = new PLH::x86Detour((char*)original, (char*)hook, &trampoline, *disassembler);
	if (!detour)
	{
		Core::Alert("No detour");
		return nullptr;
	}
	if (!detour->hook())
	{
		Core::Alert("hook failed");
		return nullptr;
	}
	return (void*)trampoline;
#else
	CrashProcDetour.install(original, hook);
	CrashProcDetour.enable();
	if (!CrashProcDetour.is_enabled())
	{
		CrashProcDetour.disable();
		return nullptr;
	}
	return (void*)CrashProcDetour.get_original_addr();
#endif
}

bool Core::hook_custom_opcodes()
{
#ifdef _WIN32
	oCrashProc = (CrashProcPtr)install_hook(CrashProc, hCrashProc);
	oCallGlobalProc = (CallGlobalProcPtr)install_hook(CallGlobalProc, hCallGlobalProc);
	return oCrashProc && oCallGlobalProc;
#else
	CrashProcDetour = (CrashProcDetour)install_hook(CrashProc, hCrashProc);
	return CrashProcDetour != NULL;
#endif
}