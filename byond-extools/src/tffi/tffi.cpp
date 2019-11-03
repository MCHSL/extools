#include "tffi.h"
#include "../dmdism/disassembler.h"
#include "../dmdism/disassembly.h"
#include "../dmdism/opcodes.h"

typedef const char* (byond_ffi_func)(int, const char**);

std::map<float, SuspendedProc*> suspended_procs;
std::map<std::string, std::map<std::string, byond_ffi_func*>> library_cache;

int result_string_id = 0;
int completed_string_id = 0;
int internal_id_string_id = 0;

void tffi_suspend(ExecutionContext* ctx)
{
	ctx->current_opcode++;
#ifdef _WIN32
	SuspendedProc* proc = Suspend(ctx, 0);
#else
	SuspendedProc* proc = Suspend(ctx);
#endif
	proc->time_to_resume = 0x7FFFFF;
	StartTiming(proc);
	float promise_id = ctx->constants->args[1].valuef;
	suspended_procs[promise_id] = proc;
	ctx->current_opcode--;
}

bool TFFI::initialize()
{
	// the CrashProc hook is currently super broken on Linux. It hooks but doesn't stop the crash.
#ifdef _WIN32
	int suspension_opcode = Core::register_opcode("TFFI_SUSPEND", tffi_suspend);
	Core::Proc internal_resolve = Core::get_proc("/datum/promise/proc/__internal_resolve");
	internal_resolve.set_bytecode(new std::vector<int>({ suspension_opcode, 0, 0, 0 }));
#endif
	result_string_id = GetStringTableIndex("result", 0, 1);
	completed_string_id = GetStringTableIndex("completed", 0, 1);
	internal_id_string_id = GetStringTableIndex("__id", 0, 1);

	return true;
}

void ffi_thread(byond_ffi_func* proc, int promise_id, int n_args, std::vector<std::string> args)
{
	std::vector<const char*> a;
	for (int i = 0; i < n_args; i++)
	{
		a.push_back(args[i].c_str());
	}
	const char* res = proc(n_args, a.data());
	SetVariable( 0x21, promise_id , result_string_id, { 0x06, (int)GetStringTableIndex(res, 0, 1) });
	SetVariable( 0x21, promise_id , completed_string_id, { 0x2A, 1 });
	float internal_id = GetVariable( 0x21, promise_id , internal_id_string_id).valuef;
	while (true)
	{
		if (suspended_procs.find(internal_id) != suspended_procs.end())
		{
			break;
		}
#ifdef _WIN32
		Sleep(1);
#else
		usleep(1000);
#endif
		//TODO: some kind of conditional variable or WaitForObject?
	}
	suspended_procs[internal_id]->time_to_resume = 1;
	suspended_procs.erase(internal_id);
}

inline void do_it(byond_ffi_func* proc, std::string promise_datum_ref, int n_args, const char** args)
{
	promise_datum_ref.erase(promise_datum_ref.begin(), promise_datum_ref.begin() + 3);
	int promise_id = std::stoi(promise_datum_ref.substr(promise_datum_ref.find("0"), promise_datum_ref.length() - 2), nullptr, 16);
	std::vector<std::string> a;
	for (int i = 3; i < n_args; i++)
	{
		a.push_back(args[i]);
	}
	std::thread t(ffi_thread, proc, promise_id, n_args - 3, a);
	t.detach();
}

extern "C" EXPORT const char* call_async(int n_args, const char** args)
{
	const char* dllname = args[1];
	const char* funcname = args[2];
	if (library_cache.find(dllname) != library_cache.end())
	{
		if (library_cache[dllname].find(funcname) != library_cache[dllname].end())
		{
			do_it(library_cache[dllname][funcname], args[0], n_args, args);
			return "";
		}
	}
#ifdef _WIN32
	HMODULE lib = LoadLibraryA(dllname);
#else
	void* lib = dlopen(dllname, 0);
#endif
	if (!lib)
	{
		return "ERROR: Could not find library!";
	}
#ifdef _WIN32
	byond_ffi_func* proc = (byond_ffi_func*)GetProcAddress(lib, funcname);
#else
	byond_ffi_func* proc = (byond_ffi_func*)dlsym(lib, funcname);
#endif
	if (!proc)
	{
		return "ERROR: Could not locate function in library!";
	}

	library_cache[dllname][funcname] = proc;
	do_it(proc, args[0], n_args, args);
	return "";
}