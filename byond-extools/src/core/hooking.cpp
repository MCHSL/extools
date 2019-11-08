#include "hooking.h"
#include "../tffi/tffi.h"
#include <chrono>
#include <fstream>
#include "json.hpp"
#include <stack>
#include "../extended_profiling/extended_profiling.h"

/*#ifdef _WIN32
static PLH::CapstoneDisassembler *disassembler;
PLH::x86Detour *CrashProcDetour;
PLH::x86Detour *CallGlobalProcDetour;
#else*/
urmem::hook CrashProcDetour;
urmem::hook CallGlobalProcDetour;
//#endif
CrashProcPtr oCrashProc;
CallGlobalProcPtr oCallGlobalProc;

//ExecutionContext* last_suspended_ec;

trvh hCallGlobalProc(char unk1, int unk2, int proc_type, unsigned int proc_id, int const_0, char unk3, int unk4, Value *argList, unsigned int argListLen, int const_0_2, int const_0_3)
{
	Core::extended_profiling_insanely_hacky_check_if_its_a_new_call_or_resume = proc_id;
	if (proc_hooks.find(proc_id) != proc_hooks.end())
	{
		trvh result = proc_hooks[proc_id](argList, argListLen);
		return result;
	}
/*#ifdef _WIN32
	trvh result = oCallGlobalProc(unk1, unk2, proc_type, proc_id, const_0, unk3, unk4, argList, argListLen, const_0_2, const_0_3);
#else*/
	trvh result = CallGlobalProcDetour.call<urmem::calling_convention::cdeclcall, trvh>(unk1, unk2, proc_type, proc_id, const_0, unk3, unk4, argList, argListLen, const_0_2, const_0_3);
//#endif
	Core::extended_profiling_insanely_hacky_check_if_its_a_new_call_or_resume = -1;
	return result;
}

void hCrashProc(char *error, int argument)
{
	if (Core::opcode_handlers.find(argument) != Core::opcode_handlers.end())
	{
		Core::opcode_handlers[argument](*Core::current_execution_context_ptr);
		return;
	}
/*#ifdef _WIN32
	oCrashProc(error, argument);
#else*/
	CrashProcDetour.call(error, argument);
//#endif
}

/*#ifdef _WIN32
void* Core::install_hook(void* original, void* hook)
{
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
}
#endif*/

bool Core::hook_custom_opcodes() {
/*#ifdef _WIN32
	oCrashProc = (CrashProcPtr)install_hook(CrashProc, hCrashProc);
	oCallGlobalProc = (CallGlobalProcPtr)install_hook(CallGlobalProc, hCallGlobalProc);
	return oCrashProc && oCallGlobalProc;
#else // casting to void* for install_hook and using urmem causes weird byond bug errors and i don't feel like debugging why*/
	CrashProcDetour.install(urmem::get_func_addr(CrashProc), urmem::get_func_addr(hCrashProc));
	oCrashProc = (CrashProcPtr)CrashProcDetour.get_original_addr();
#ifdef _WIN32
	CallGlobalProcDetour.install(urmem::get_func_addr(CallGlobalProc), urmem::get_func_addr(hCallGlobalProc));
	oCallGlobalProc = (CallGlobalProcPtr)CallGlobalProcDetour.get_original_addr();
#else
	oCallGlobalProc = CallGlobalProc;
#endif
	return oCrashProc && CallGlobalProc;
	return oCrashProc;
//#endif
}