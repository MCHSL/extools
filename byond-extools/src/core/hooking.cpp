#include "hooking.h"

PLH::CapstoneDisassembler* disassembler;
PLH::x86Detour* CrashProcDetour;
CrashProcPtr oCrashProc;

void hCrashProc(char* error, int argument)
{
	if (Core::opcode_handlers.find(argument) != Core::opcode_handlers.end())
	{
		Core::opcode_handlers[argument](*Core::current_execution_context_ptr);
		return;
	}
	oCrashProc(error, argument);
}

bool Core::hook_em()
{
	disassembler = new PLH::CapstoneDisassembler(PLH::Mode::x86);
	if (!disassembler)
	{
		return false;
	}
	std::uint64_t sendMapsTrampoline;
	CrashProcDetour = new PLH::x86Detour((char*)CrashProc, (char*)&hCrashProc, &sendMapsTrampoline, *disassembler);
	if (!CrashProcDetour)
	{
		return false;
	}
	if (!CrashProcDetour->hook())
	{
		return false;
	}
	oCrashProc = PLH::FnCast(sendMapsTrampoline, oCrashProc);
	if (!oCrashProc)
	{
		CrashProcDetour->unHook();
		return false;
	}
	return true;
}