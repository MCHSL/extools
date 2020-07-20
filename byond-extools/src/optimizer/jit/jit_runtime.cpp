#include "../../core/core.h"
#include "DMCompiler.h"
#include "JitContext.h"
#include "jit_runtime.h"

#define NOMINMAX
#include <windows.h>

using namespace dmjit;

static uint32_t wrapper_id = 0;

static SuspendPtr oSuspend;
static ProcConstants* hSuspend(ExecutionContext* ctx, int unknown)
{
	const uint32_t proc_id = ctx->constants->proc_id;
	if (proc_id == wrapper_id)
	{
		auto* const jc = reinterpret_cast<JitContext*>(ctx->constants->args[1].value);
		jc->suspended = true;
	}
	return oSuspend(ctx, unknown);
}

static ExecutionContext* __fastcall fuck(ExecutionContext* dmctx)
{
	ProcConstants* const pc = dmctx->constants;
	// This flag might actually be something like "suspendable", doubt it though
	dmctx->paused = true;
	auto* const jctx = reinterpret_cast<JitContext*>(pc->args[1].value);
	jctx->suspended = false;
	// The first 2 arguments are base and context, we pass the pointer to the third arg onwards.
	const unsigned int args_len = std::max(0, pc->arg_count - 2);
	const Value retval = JitEntryPoint(reinterpret_cast<void*>(pc->args[0].value), args_len, pc->args + 2, pc->src, pc->usr, reinterpret_cast<JitContext*>(pc->args[1].value));
	dmctx->stack_size++;
	dmctx->stack[dmctx->stack_size - 1] = retval;
	dmctx->dot = retval;
	return dmctx;
}

__declspec(naked) ExecutionContext* just_before_execution_hook()
{
	__asm {
		mov eax, DWORD PTR[Core::current_execution_context_ptr]
		mov eax, DWORD PTR[eax]
		mov edx, DWORD PTR[eax]
		mov ecx, DWORD PTR[wrapper_id]
		cmp DWORD PTR[edx], ecx
		jne yeet
		mov ecx, eax
		call fuck // fastcall - the context must be passed in ecx
		xor edx, edx
		yeet :
		ret
	}
	// xoring a register sets the zero flag. This hook is preceded by a TEST and followed by a JE which jumps over the bytecode interpreting part.
	// So we're pretty much skipping the entire bytecode bit and go faster.
}

static void hook_resumption()
{
	wrapper_id = Core::get_proc("/proc/jit_wrapper").id;
	oSuspend = Core::install_hook(Suspend, hSuspend);

	DWORD old_prot;
	char* remember_to_return_context = (char*)Pocket::Sigscan::FindPattern("byondcore.dll", "A1 ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? 66 FF 40 14 EB 18 8B 00 80 78 10 22 A1 ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? EB 05 A1 ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 89 ?? ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 89");
	VirtualProtect(remember_to_return_context, 5, PAGE_READWRITE, &old_prot);
	remember_to_return_context[0] = (char)0xE8; //CALL
	*(int*)(remember_to_return_context + 1) = (int)&just_before_execution_hook - (int)remember_to_return_context - 5;
	VirtualProtect(remember_to_return_context, 5, old_prot, &old_prot);

	remember_to_return_context = (char*)Pocket::Sigscan::FindPattern("byondcore.dll", "A1 ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 89 ?? ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 89 ?? ?? ?? ?? ?? 0F B7 48 14 8B 78 10 8B F1 8B 14 B7 81 FA");
	VirtualProtect(remember_to_return_context, 5, PAGE_READWRITE, &old_prot);
	remember_to_return_context[0] = (char)0xE8; //CALL
	*(int*)(remember_to_return_context + 1) = (int)&just_before_execution_hook - (int)remember_to_return_context - 5;
	VirtualProtect(remember_to_return_context, 5, old_prot, &old_prot);

	remember_to_return_context = (char*)Pocket::Sigscan::FindPattern("byondcore.dll", "A1 ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? EB 05 E8 ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 89 ?? ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 89 ?? ?? ?? ?? ?? 0F B7 48 14 8B 78 10 8B F1 8B 14 B7 81 FA ?? ?? ?? ?? 0F 87 ?? ?? ?? ?? FF 24 ?? ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 8D 41 01 0F B7 C8");
	VirtualProtect(remember_to_return_context, 5, PAGE_READWRITE, &old_prot);
	remember_to_return_context[0] = (char)0xE8; //CALL
	*(int*)(remember_to_return_context + 1) = (int)&just_before_execution_hook - (int)remember_to_return_context - 5;
	VirtualProtect(remember_to_return_context, 5, old_prot, &old_prot);
}

trvh JitEntryPoint(void* code_base, const unsigned int args_len, Value* const args, const Value src, const Value usr, JitContext* ctx)
{
	if (!ctx)
	{
		ctx = new JitContext();
	}

	const Proc code = reinterpret_cast<Proc>(code_base);

	const ProcResult res = code(ctx, args_len, args, src, usr);

	switch (res)
	{
	case ProcResult::Success:
	{
		/*if (ctx->CountFrame() != 1)
		{
			__debugbreak();
			break;
		}*/
		const Value return_value = *--ctx->stack_top;
		if (!ctx->stack_frame)
		{
			// We've returned from the only proc in this context's stack, so it is no longer needed.
			delete ctx;
		}
		return return_value;
	}
	case ProcResult::Yielded:
	{
		const Value dot = *--ctx->stack_top;
		// No need to do anything here, the JitContext is already marked as suspended
		if (!ctx->suspended)
		{
			// Let's check though, just to make sure
			__debugbreak();
		}
		return dot;
	}
	case ProcResult::Sleeping:
	{
		const Value dot = *--ctx->stack_top;
		const Value sleep_time = *--ctx->stack_top;
		// We are inside of a DM proc wrapper. It will be suspended by this call,
		// and the suspension will propagate to parent jit calls.
		ProcConstants* suspended = Suspend(Core::get_context(), 0);
		suspended->time_to_resume = static_cast<unsigned int>(sleep_time.valuef / Value::World().get("tick_lag").valuef);
		StartTiming(suspended);
		return dot;
	}
	}
	__debugbreak();
	return Value::Null();
}

bool enable_jit()
{
	static bool jit_enabled = false;
	if(jit_enabled)
	{
		return true;
	}
	
	if(!Core::initialize())
	{
		return false;
	}

	hook_resumption();
	jit_enabled = true;
	return true;
}