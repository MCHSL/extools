#include "tffi.h"
#include "../dmdism/disassembler.h"
#include "../dmdism/disassembly.h"
#include "../dmdism/opcodes.h"

#include <condition_variable>
#include <mutex>

typedef const char* (byond_ffi_func)(int, const char**);

std::map<float, Core::ResumableProc> suspended_procs;
std::map<std::string, std::map<std::string, byond_ffi_func*>> library_cache;

std::uint32_t result_string_id = 0;
std::uint32_t completed_string_id = 0;
std::uint32_t internal_id_string_id = 0;

std::condition_variable unsuspend_ready_cv;
std::mutex unsuspend_ready_mutex;

void tffi_suspend(ExecutionContext* ctx)
{
	float promise_id = ctx->constants->args[1].valuef;
	std::lock_guard<std::mutex> lk(unsuspend_ready_mutex);
	suspended_procs.insert({promise_id, Core::SuspendCurrentProc()});
	unsuspend_ready_cv.notify_all();
}

bool TFFI::initialize()
{
	// the CrashProc hook is currently super broken on Linux. It hooks but doesn't stop the crash.
#ifdef _WIN32
	std::uint32_t suspension_opcode = Core::register_opcode("TFFI_SUSPEND", tffi_suspend);
	Core::Proc& internal_resolve = Core::get_proc("/datum/promise/proc/__internal_resolve");
	internal_resolve.set_bytecode({ suspension_opcode, 0, 0, 0 });
#endif
	result_string_id = Core::GetStringId("result");
	completed_string_id = Core::GetStringId("completed");
	internal_id_string_id = Core::GetStringId("__id");
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
	SetVariable( DataType::DATUM, promise_id, result_string_id, { DataType::STRING, (int)Core::GetStringId(res) });
	SetVariable( DataType::DATUM, promise_id, completed_string_id, { DataType::NUMBER, 1 });
	float internal_id = GetVariable( DataType::DATUM, promise_id , internal_id_string_id).valuef;
	std::unique_lock<std::mutex> lk(unsuspend_ready_mutex);
	unsuspend_ready_cv.wait(lk, [internal_id] { return suspended_procs.find(internal_id) != suspended_procs.end();  });
	suspended_procs.at(internal_id).resume();
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