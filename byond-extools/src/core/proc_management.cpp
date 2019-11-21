#include "proc_management.h"
#include "../dmdism/disassembly.h"
#include "../dmdism/disassembler.h"
#include "../extended_profiling/extended_profiling.h"

std::vector<Core::Proc> procs_by_id;
std::unordered_map<std::string, Core::Proc> procs_by_name;
std::unordered_map<unsigned int, bool> extended_profiling_procs;
std::unordered_map<unsigned int, ProcHook> proc_hooks;

Core::Proc::Proc(std::string name)
{
	*this = procs_by_name[name];
}

Core::Proc::Proc(std::uint32_t id)
{
	*this = procs_by_id[id];
}

void Core::Proc::set_bytecode(std::vector<std::uint32_t>* new_bytecode)
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

void Core::Proc::set_bytecode(std::uint32_t* new_bytecode)
{
	setup_entry_bytecode->bytecode = new_bytecode;
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

std::uint32_t* Core::Proc::get_bytecode()
{
	return setup_entry_bytecode->bytecode;
}

std::uint16_t Core::Proc::get_bytecode_length()
{
	return setup_entry_bytecode->bytecode_length;
}

std::uint16_t Core::Proc::get_local_varcount()
{
	Core::Alert(std::to_string(setup_entry_varcount->local_var_count));
	Core::Alert(std::to_string((int)proc_setup_table[varcount_idx]));
	Core::Alert(std::to_string(setup_entry_bytecode->local_var_count));
	return setup_entry_varcount->local_var_count;
}

ProfileInfo* Core::Proc::profile()
{
	return GetProfileInfo(id);
}

void Core::Proc::extended_profile()
{
	procs_to_profile[id] = true;
}

void Core::Proc::hook(ProcHook hook_func)
{
	proc_hooks[id] = hook_func;
}

// This is not thread safe - only use when you are on the main thread, such as hooks or custom opcodes
Value Core::Proc::call(std::vector<Value>& arguments, Value usr, Value src)
{
	return CallGlobalProc(usr.type, usr.value, 2, id, 0, src.type, src.value, arguments.data(), arguments.size(), 0, 0);
}

Disassembly Core::Proc::disassemble()
{
	Disassembly d = Core::get_disassembly(name);
	d.proc = *this;
	return d;
}

void Core::Proc::assemble(Disassembly disasm)
{
	std::vector<std::uint32_t>* bc = disasm.assemble();
	set_bytecode(bc);
	setup_entry_bytecode->bytecode_length = bc->size();
	//proc_table_entry->local_var_count_idx = Core::get_proc("/proc/twelve_locals").proc_table_entry->local_var_count_idx;
}

Core::Proc Core::get_proc(std::string name)
{
	return procs_by_name[name];
}

Core::Proc Core::get_proc(unsigned int id)
{
	return procs_by_id[id];
}

const std::vector<Core::Proc>& Core::get_all_procs()
{
	return procs_by_id;
}

Disassembly Core::disassemble_raw(std::vector<int> bytecode)
{
	return Disassembler(std::vector<std::uint32_t>(bytecode.begin(), bytecode.end()), procs_by_id).disassemble();
}

bool Core::populate_proc_list()
{
	unsigned int i = 0;
	while (true)
	{
		ProcArrayEntry* entry = GetProcArrayEntry(i);
		if (!entry)
		{
			break;
		}
		Proc p = Proc();
		p.id = i;
		p.name = GetStringTableEntry(entry->procPath)->stringData;
		p.simple_name = p.name.substr(p.name.rfind("/") + 1);
		p.proc_table_entry = entry;
		p.setup_entry_bytecode = proc_setup_table[entry->bytecode_idx];
		p.setup_entry_varcount = proc_setup_table[entry->local_var_count_idx];
		p.bytecode_idx = entry->bytecode_idx;
		p.varcount_idx = entry->local_var_count_idx;
		procs_by_id.push_back(p);
		procs_by_name[p.name] = p;
		i++;
	}
	return true;
}