#include "hooking.h"
#include "../tffi/tffi.h"
#include <chrono>
#include <fstream>
#include "json.hpp"
#include <stack>
#include "../extended_profiling/extended_profiling.h"

#ifdef _WIN32
static PLH::CapstoneDisassembler *disassembler;
PLH::x86Detour *CrashProcDetour;
PLH::x86Detour *CallGlobalProcDetour;
#else
urmem::hook CrashProcDetour;
urmem::hook CallGlobalProcDetour;
#endif
CrashProcPtr oCrashProc;
CallGlobalProcPtr oCallGlobalProc;

//ExecutionContext* last_suspended_ec;

trvh hCallGlobalProc(char unk1, int unk2, int proc_type, unsigned int proc_id, int const_0, char unk3, int unk4, Value *argList, unsigned int argListLen, int const_0_2, int const_0_3)
{
	if (proc_hooks.find(proc_id) != proc_hooks.end())
	{
		trvh result = proc_hooks[proc_id](argList, argListLen);
		return result;
	}
	trvh result = oCallGlobalProc(unk1, unk2, proc_type, proc_id, const_0, unk3, unk4, argList, argListLen, const_0_2, const_0_3);
	return result;
}

void hCrashProc(char *error, int argument)
{
	if (Core::opcode_handlers.find(argument) != Core::opcode_handlers.end())
	{
		Core::opcode_handlers[argument](*Core::current_execution_context_ptr);
		return;
	}
	oCrashProc(error, argument);
}

#ifdef _WIN32
Hook* Core::install_hook(void *original, void *hook)
{

	disassembler = new PLH::CapstoneDisassembler(PLH::Mode::x86);
	if (!disassembler)
	{
		return nullptr;
	}
	std::uint64_t trampoline;
	PLH::x86Detour *detour = new PLH::x86Detour((char *)original, (char *)hook, &trampoline, *disassembler);
	Hook *hook_struct = new Hook;
	hook_struct->hook = detour;
	hook_struct->trampoline = (void *)trampoline;
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
	return hook_struct;
}
#endif

bool Core::hook_custom_opcodes()
{
#ifdef _WIN32
	Hook* crashProc = install_hook(CrashProc, hCrashProc);
	if (!crashProc)
		return false;
	oCrashProc = (CrashProcPtr)crashProc->trampoline;
	CrashProcDetour = crashProc->hook;
	Hook* callGlobalProc = install_hook(CallGlobalProc, hCallGlobalProc);
	if (!callGlobalProc)
		return false;
	oCallGlobalProc = (CallGlobalProcPtr)callGlobalProc->trampoline;
	CallGlobalProcDetour = callGlobalProc->hook;
	return oCrashProc && oCallGlobalProc;
	return true;
#else // casting to void* for install_hook and using urmem causes weird byond bug errors and i don't feel like debugging why
	CrashProcDetour.install(urmem::get_func_addr(CrashProc), urmem::get_func_addr(hCrashProc));
	oCrashProc = (CrashProcPtr)CrashProcDetour.get_original_addr();
	/*CallGlobalProcDetour.install(urmem::get_func_addr(CallGlobalProc), urmem::get_func_addr(hCallGlobalProc));
	oCallGlobalProc = (CallGlobalProcPtr)CallGlobalProcDetour.get_original_addr();
	return oCrashProc && CallGlobalProc;*/
	return oCrashProc;
#endif
}