#pragma once
#include "core.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Core
{
	struct Proc
	{
		std::string name;

		ProcArrayEntry* proc_table_entry;
		ProcSetupEntry* setup_entry_bytecode;
		ProcSetupEntry* setup_entry_varcount;

		int bytecode_idx;
		int varcount_idx;

		int* original_bytecode_ptr = nullptr;

		void set_bytecode(std::vector<int>* new_bytecode);
		int* get_bytecode();
		int get_bytecode_length();
		void reset_bytecode();

		int get_local_varcount();
	};

	Proc get_proc(std::string name);
	Proc get_proc(unsigned int id);

	bool populate_proc_list();
}