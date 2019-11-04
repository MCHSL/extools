#include "hooking.h"
#include "../tffi/tffi.h"
#include <chrono>
#include <fstream>
#include "json.hpp"
#include <stack>
#include "../extended_profiling/extended_profiling.h"

#ifdef _WIN32
static PLH::CapstoneDisassembler* disassembler;
#else
urmem::hook CrashProcDetour;
#endif
CrashProcPtr oCrashProc;
CallGlobalProcPtr oCallGlobalProc;

//ExecutionContext* last_suspended_ec;

trvh __cdecl hCallGlobalProc(char unk1, int unk2, int proc_type, unsigned int proc_id, int const_0, char unk3, int unk4, Value* argList, unsigned int argListLen, int const_0_2, int const_0_3)
{
	if (proc_hooks.find(proc_id) != proc_hooks.end())
	{
		trvh result = proc_hooks[proc_id](argList, argListLen);
		return result;
	}
	trvh result = oCallGlobalProc(unk1, unk2, proc_type, proc_id, const_0, unk3, unk4, argList, argListLen, const_0_2, const_0_3);
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
	detour.install(original, hook);
	detour.enable();
	if (!detour.is_enabled())
	{
		detour.disable();
		return nullptr;
	}
	return (void*)detour;
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
	return CrashProcDetour;
#endif
}