#include "crash_guard.h"
#include <fstream>
#include <ctime>

#ifdef WIN32
#include <windows.h>
#include "exception_codes.h"

RuntimePtr oRuntimeLL;

std::string ErrorToString(int id)
{
	return exc_code_to_msg.find(id) != exc_code_to_msg.end() ? exc_code_to_msg.at(id) : "";
}

extern "C" __declspec(dllexport) const char* force_crash(int n, const char* args)
{
	volatile int a = 1;
	volatile int b = 0;
	int x = a / b;
	return "";
}

LONG WINAPI all_the_broken_things_that_byond_made(_EXCEPTION_POINTERS* ExceptionInfo)
{
	std::ofstream dump("crashdump.log");
	std::time_t t = std::time(nullptr);
	std::string timestamp = "UNKNOWN TIME";
	char buf[64];
	if (std::strftime(buf, sizeof(buf), "%c", std::localtime(&t)))
	{
		timestamp = std::string(buf);
	}
	dump << "An exception has occured at " << timestamp << "\n";
	dump << "Exception: " << std::hex << ExceptionInfo->ExceptionRecord->ExceptionCode << ": " << ErrorToString(ExceptionInfo->ExceptionRecord->ExceptionCode) << "\n";
	dump.flush();
	HMODULE hModule = NULL;
	GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCTSTR)ExceptionInfo->ExceptionRecord->ExceptionAddress, &hModule);
	std::string module;
	module.resize(128);
	if (hModule)
	{
		module.resize(GetModuleFileNameA(hModule, module.data(), module.size()));
	}
	dump << "Address: " << ExceptionInfo->ExceptionRecord->ExceptionAddress << " (" << module << ")\n";
	dump << "Offset: " << ((DWORD)ExceptionInfo->ExceptionRecord->ExceptionAddress - (DWORD)hModule) << "\n";
	dump.flush();
	dump << "Parameters:\n";
	for (int i = 0; i < ExceptionInfo->ExceptionRecord->NumberParameters; i++)
	{
		dump << "\t" << i << ": " << ExceptionInfo->ExceptionRecord->ExceptionInformation[i] << "\n";
	}
	dump.flush();
	ExecutionContext* ctx = Core::get_context();
	if (!ctx)
	{
		dump.close();
		return EXCEPTION_CONTINUE_SEARCH;
	}
	dump << "\nCall stack:\n";
	ExecutionContext* dctx = ctx;
	do
	{
		dump << "\t" << Core::get_proc(dctx->constants->proc_id).raw_path << "\n";
	} while (dctx = dctx->parent_context);
	dump << "\nArguments:\n";
	for (int i = 0; i < ctx->constants->arg_count; i++)
	{
		dump << "\t" << ctx->constants->args[i].type << " " << (ctx->constants->args[i].type == 0x2A ? ctx->constants->args[i].valuef : ctx->constants->args[i].value) << "\n";
	}
	dump << "\nLocal variables:\n";
	for (int i = 0; i < ctx->local_var_count; i++)
	{
		dump << "\t" << ctx->local_variables[i].type << " " << (ctx->local_variables[i].type == 0x2A ? ctx->local_variables[i].valuef : ctx->local_variables[i].value) << "\n";
	}


	return EXCEPTION_CONTINUE_SEARCH;
}

void dump_oor()
{
	std::ofstream dump("out_of_resources.log");
	std::time_t t = std::time(nullptr);
	std::string timestamp = "UNKNOWN TIME";
	char buf[64];
	if (std::strftime(buf, sizeof(buf), "%c", std::localtime(&t)))
	{
		timestamp = std::string(buf);
	}
	dump << "Out of resources runtime occured at" << timestamp << "\n";
	ExecutionContext* ctx = Core::get_context();
	if (!ctx)
	{
		dump << "Unable to acquire execution context. Sorry.\n";
		return;
	}
	dump << "Call stack:\n";
	do
	{
		dump << "Proc: " << Core::get_proc(ctx->constants->proc_id).raw_path << "\n";
		dump << "File: " << Core::GetStringFromId(ctx->dbg_proc_file) << "\n";
		dump << "Line: " << ctx->dbg_current_line << "\n";
		dump << "usr: " << Core::stringify(ctx->constants->usr) << "\n";
		dump << "src: " << Core::stringify(ctx->constants->src) << "\n";
		dump << "dot: " << Core::stringify(ctx->dot) << "\n";
		dump << "\nArguments:\n";
		for (int i = 0; i < ctx->constants->arg_count; i++)
		{
			dump << "\t" << (int)ctx->constants->args[i].type << " " << (ctx->constants->args[i].type == 0x2A ? ctx->constants->args[i].valuef : ctx->constants->args[i].value) << "\n";
		}
		dump << "\nLocal variables:\n";
		for (int i = 0; i < ctx->local_var_count; i++)
		{
			dump << "\t" << (int)ctx->local_variables[i].type << " " << (ctx->local_variables[i].type == 0x2A ? ctx->local_variables[i].valuef : ctx->local_variables[i].value) << "\n";
		}
		dump.flush();
	} while (ctx = ctx->parent_context);
}

void hRuntimeLL(const char* err)
{
	if (strcmp(err, "Out of resources!") == 0)
	{
		dump_oor();
	}
	oRuntimeLL(err);
}

bool enable_crash_guard()
{
	AddVectoredExceptionHandler(0, all_the_broken_things_that_byond_made);
	return true;
}

extern "C" EXPORT const char* enable_resource_leak_locator(int arg_n, const char** c)
{
	Core::initialize();
	oRuntimeLL = Core::install_hook(Runtime, hRuntimeLL);
	return "";
}

#else

bool enable_crash_guard()
{
	return false;
}

#endif