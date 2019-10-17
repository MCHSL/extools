#include "proc_management.h"

std::vector<Core::Proc> procs_by_id;
std::unordered_map<std::string, Core::Proc> procs_by_name;

void Core::Proc::set_bytecode(std::vector<int>* new_bytecode)
{
	if (original_bytecode_ptr)
	{
		delete setup_entry_bytecode->bytecode;
	}
	else
	{
		original_bytecode_ptr = setup_entry_bytecode->bytecode;
	}
	setup_entry_bytecode->bytecode = new_bytecode->data();
}

void Core::Proc::reset_bytecode()
{
	if (!original_bytecode_ptr)
	{
		return;
	}
	delete setup_entry_bytecode->bytecode;
	setup_entry_bytecode->bytecode = original_bytecode_ptr;
	original_bytecode_ptr = nullptr;
}

int* Core::Proc::get_bytecode()
{
	return setup_entry_bytecode->bytecode;
}

int Core::Proc::get_bytecode_length()
{
	return setup_entry_bytecode->bytecode_length;
}

int Core::Proc::get_local_varcount()
{
	return setup_entry_varcount->local_var_count;
}

Core::Proc get_proc(std::string name)
{
	return procs_by_name[name];
}

Core::Proc get_proc(unsigned int id)
{
	return procs_by_id[id];
}

bool Core::populate_proc_list()
{
	unsigned int i = 0;
	while (true)
	{
		Proc p = Proc();
		ProcArrayEntry* entry = GetProcArrayEntry(i);
		if (!entry)
		{
			break;
		}
		p.name = GetStringTableEntry(entry->procPath)->stringData;
		p.setup_entry_bytecode = proc_setup_table[entry->bytecode_idx];
		p.setup_entry_varcount = proc_setup_table[entry->local_var_count_idx];
		p.bytecode_idx = entry->bytecode_idx;
		p.varcount_idx = entry->local_var_count_idx;
		procs_by_id.push_back(p);
		procs_by_name[p.name] = p;
		i++;
	}
}