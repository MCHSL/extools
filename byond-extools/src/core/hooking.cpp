#include "hooking.h"

#ifdef _WIN32
static PLH::CapstoneDisassembler* disassembler;
#else
urmem::hook CrashProcDetour;
#endif
CrashProcPtr oCrashProc;

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
	return oCrashProc;
#else
	CrashProcDetour = (CrashProcDetour)install_hook(CrashProc, hCrashProc);
	return CrashProcDetour;
#endif
}