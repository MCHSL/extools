#pragma once
#include "core.h"
#include <string>
#include <vector>
#include <unordered_map>

typedef trvh(*ProcHook)(Value* args, unsigned int args_len);

class Disassembly;

namespace Core
{
	struct Proc
	{
		Proc() {};
		Proc(std::string name);
		std::string name;
		short id;

		ProcArrayEntry* proc_table_entry;
		ProcSetupEntry* setup_entry_bytecode;
		ProcSetupEntry* setup_entry_varcount;

		unsigned short bytecode_idx;
		unsigned short varcount_idx;

		int* original_bytecode_ptr = nullptr;

		void set_bytecode(std::vector<int>* new_bytecode);
		int* get_bytecode();
		int get_bytecode_length();
		void reset_bytecode();

		int get_local_varcount();
		Disassembly disassemble();
		void assemble(Disassembly disasm);

		ProfileInfo* profile();
		void hook(ProcHook hook_func);
		Value call(std::vector<Value> arguments, Value usr = Value::Null(), Value src = Value::Null());

		bool operator<(const Proc& rhs) const
		{
			return id < rhs.id;
		}

		bool operator==(const Proc& rhs) const
		{
			return id == rhs.id;
		}
	};

	Proc get_proc(std::string name);
	Proc get_proc(unsigned int id);
	Proc get_proc(int* bytecode);
	const std::vector<Proc> get_all_procs();

	bool populate_proc_list();
	Disassembly disassemble_raw(std::vector<int> bytecode);
}

extern std::vector<Core::Proc> procs_by_id;
extern std::unordered_map<int*, Core::Proc> procs_by_bytecode;
extern std::unordered_map<unsigned int, ProcHook> proc_hooks;
extern std::unordered_map<unsigned int, bool> extended_profiling_procs;