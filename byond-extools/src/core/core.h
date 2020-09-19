#pragma once
#include "sigscan/sigscan.h"
#include "byond_structures.h"
#include "byond_functions.h"
#include "hooking.h"
#include "proc_management.h"
#ifndef _WIN32
#include <dlfcn.h>
#endif

#include <map>
#include <string>
#include <cmath>
#include <vector>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

typedef void(*opcode_handler)(ExecutionContext* ctx);

#define MIN_COMPATIBLE_MAJOR 512
#define MIN_COMPATIBLE_MINOR 1484

#define MANAGED(x) Core::ManagedString(x)

extern int ByondVersion;
extern int ByondBuild;

namespace Core
{
	const char* const SUCCESS = "SUCCESS";
	const char* const FAIL = "FAIL";

	//not exactly a byond structure so it's here for now

	class ManagedString
	{
		// Represents a BYOND string. Refcount gets updated when created and destroyed,
		// which should help with memory usage and premature string deletion.
		// Use when passing the string to proc callbacks for example.
	public:
		ManagedString(unsigned int id);
		ManagedString(std::string str); //TODO: find Inc- and DecRefCount and manage the refcount properly
		ManagedString(const ManagedString& other);
		~ManagedString();

		operator unsigned int()
		{
			return string_id;
		}

		operator int()
		{
			return string_id;
		}

		operator const char* ()
		{
			return string_entry->stringData;
		}

		operator std::string()
		{
			return string_entry->stringData;
		}

	protected:
		unsigned int string_id;
		String* string_entry;
	};

	class ResumableProc
	{
	public:
		ResumableProc(const ResumableProc& other);
		static ResumableProc FromCurrent();
		void resume();

	protected:
		SuspendedProc* proc;

	private:
		ResumableProc(SuspendedProc* sp) : proc(sp) { sp->time_to_resume = 1; }

	};

	extern std::map<unsigned int, opcode_handler> opcode_handlers;
	extern std::map<std::string, unsigned int> name_to_opcode;
	extern ExecutionContext** current_execution_context_ptr;
	extern ExecutionContext** parent_context_ptr_hack;
	extern ProcSetupEntry** proc_setup_table;
	extern unsigned int* some_flags_including_profile;
	extern unsigned int* name_table_id_ptr;
	extern unsigned int* name_table;
	extern Value* global_var_table;
	extern TableHolder2* obj_table;
	extern TableHolder2* datum_table;
	extern TableHolder2* list_table; //list list honk
	extern TableHolder2* mob_table;

	extern RawDatum*** datum_pointer_table;
	extern unsigned int* datum_pointer_table_length;

	extern std::unordered_map<std::string, Value*> global_direct_cache;
	void global_direct_set(std::string name, Value val);
	Value global_direct_get(std::string name);


	//extern std::vector<bool> codecov_executed_procs;
	unsigned int GetStringId(std::string str, bool increment_refcount = 0);
	ManagedString GetManagedString(std::string str);
	void FreeByondString(std::string s);
	void FreeByondString(unsigned int id);
	std::string GetStringFromId(unsigned int id);
	RawDatum* GetDatumPointerById(unsigned int id);
	Value get_turf(int x, int y, int z);
	extern unsigned int extended_profiling_insanely_hacky_check_if_its_a_new_call_or_resume;
	ExecutionContext* get_context();
	ExecutionContext* _get_parent_context();
	unsigned int register_opcode(std::string name, opcode_handler handler);
	void Alert(const std::string& what);
	void Alert(int what);
	bool initialize();
	extern bool initialized;
	Value get_stack_value(unsigned int which);
	void stack_pop(unsigned int how_many);
	void stack_push(Value val);
	bool enable_profiling();
	bool disable_profiling();
	std::string type_to_text(unsigned int type);
	std::string stringify(Value val);
	void disconnect_client(unsigned int id);
	std::uint32_t get_socket_from_client(unsigned int id);
	void cleanup();
	void alert_dd(std::string msg);
	ResumableProc SuspendCurrentProc();
}