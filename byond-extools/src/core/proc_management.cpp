#include "proc_management.h"
#include "../dmdism/disassembly.h"
#include "../dmdism/disassembler.h"
#include "../extended_profiling/extended_profiling.h"

std::vector<Core::Proc> procs_by_id;
std::unordered_map<std::string, std::vector<Core::Proc>> procs_by_name;
std::unordered_map<std::string, std::string> procs_by_inherit;
//The above takes in a proc for some multiple-inherited object, and returns the actual proc to call
//Like if you input as a key "/datum/disease_ability/symptom/powerful/heal/starlight/temperature_expose" it will output "/datum/proc/temperature_expose",
//since none of those other classes overwrite the datum default.
std::unordered_map<unsigned int, bool> extended_profiling_procs;
std::unordered_map<unsigned int, ProcHook> proc_hooks;

Core::Proc::Proc(std::string name, unsigned int override_id)
{
	size_t proc_pos = name.find("/proc/");
	proc_pos = (proc_pos == std::string::npos ? name.find("/verb/") : proc_pos);
	if (proc_pos != std::string::npos)
	{
		name.erase(proc_pos, 5);
	}
	*this = procs_by_name.at(name).at(override_id);
}

Core::Proc::Proc(std::uint32_t id)
{
	*this = procs_by_id.at(id);
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

std::uint16_t Core::Proc::get_local_varcount() //TODO: this is broken
{
	//Core::Alert(std::to_string(setup_entry_varcount->local_var_count));
	//Core::Alert(std::to_string((int)proc_setup_table[varcount_idx]));
	//Core::Alert(std::to_string(setup_entry_bytecode->local_var_count));
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
Value Core::Proc::call(std::vector<Value> arguments, Value usr, Value src)
{
	return CallGlobalProc(usr.type, usr.value, 2, id, 0, src.type, src.value, arguments.data(), arguments.size(), 0, 0);
}

Disassembly Core::Proc::disassemble()
{
	Disassembly d = Core::get_disassembly(name, override_id);
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

Core::Proc Core::get_proc(std::string name, unsigned int override_id)
{
	size_t proc_pos = name.find("/proc/");
	proc_pos = (proc_pos == std::string::npos ? name.find("/verb/") : proc_pos);
	if (proc_pos != std::string::npos)
	{
		name.erase(proc_pos, 5);
	}
	//Core::Alert("Attempting to get proc " + name + ", override " + std::to_string(override_id));
	if (procs_by_inherit.count(name)) // If this is the exact name of the proc to be used
	{
		return procs_by_name.at(procs_by_inherit.at(name)).at(override_id);
	}
	else // If we need to go up higher in the inheritence tree before getting the correct proc
	{
		std::string newname = name;
		std::regex strip("(\\/[\\w_\\d]+)+(?:\\/[\\w_\\d]+)"); // Captures one group: the location of the lowest type of the inheritence chain before the proc name
		std::smatch matchstrip;
		while (std::regex_search(newname,matchstrip,strip))
		{
			if (matchstrip.size() != 2)
				break;
			newname = newname.erase(newname.find(matchstrip[1].str), matchstrip[1].length);
			if (procs_by_name.count(newname)) // If this is the exact name of the proc to be used
			{
				procs_by_inherit[name] = newname; // Cache it for later so we don't have to do this shit again
				return procs_by_name.at(newname).at(override_id);
			}
		}
	}
	
}

Core::Proc Core::get_proc(unsigned int id)
{
	return procs_by_id.at(id);
}

Core::Proc Core::get_proc(ExecutionContext* ctx)
{
	return get_proc(ctx->constants->proc_id);
}

std::vector<Core::Proc>& Core::get_all_procs()
{
	return procs_by_id;
}

Disassembly Core::disassemble_raw(std::vector<int> bytecode)
{
	return Disassembler(std::vector<std::uint32_t>(bytecode.begin(), bytecode.end()), procs_by_id).disassemble();
}

bool Core::populate_proc_list() // Populates proc_by_name and proc_by_id (and some initial stuff for proc_by_inherit)
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
		p.raw_path = p.name;
		size_t proc_pos = p.name.find("/proc/");
		proc_pos = (proc_pos == std::string::npos ? p.name.find("/verb/") : proc_pos);
		if (proc_pos != std::string::npos)
		{
			p.name.erase(proc_pos, 5);
		}
		p.simple_name = p.name.substr(p.name.rfind("/") + 1);
		p.proc_table_entry = entry;
		p.setup_entry_bytecode = proc_setup_table[entry->bytecode_idx];
		p.setup_entry_varcount = proc_setup_table[entry->local_var_count_idx];
		p.bytecode_idx = entry->bytecode_idx;
		p.varcount_idx = entry->local_var_count_idx;
		if (procs_by_name.find(p.name) == procs_by_name.end())
		{
			procs_by_name[p.name] = std::vector<Core::Proc>();
		}
		p.override_id = procs_by_name.at(p.name).size();
		procs_by_name[p.name].push_back(p);
		procs_by_id.push_back(p);
		procs_by_inherit[p.name] = p.name;
		i++;
	}
	return true;
}