#include "proc_management.h"
#include "../dmdism/disassembly.h"
#include "../dmdism/disassembler.h"
#include "../extended_profiling/extended_profiling.h"
#include <optional>

std::vector<Core::Proc> procs_by_id;
std::unordered_map<std::string, std::vector<unsigned int>> procs_by_name;
std::unordered_map<unsigned int, bool> extended_profiling_procs;
std::unordered_map<unsigned int, ProcHook> proc_hooks;

void strip_proc_path(std::string& name)
{
	size_t proc_pos = name.find("/proc/");
	proc_pos = (proc_pos == std::string::npos ? name.find("/verb/") : proc_pos);
	if (proc_pos != std::string::npos)
	{
		name.erase(proc_pos, 5);
	}
}

void Core::Proc::set_bytecode(std::vector<std::uint32_t>&& new_bytecode)
{
	if (!original_bytecode_ptr)
	{
		original_bytecode_ptr = setup_entry_bytecode->bytecode;
	}

	bytecode = std::move(new_bytecode);
	setup_entry_bytecode->bytecode = bytecode.data();
}

void Core::Proc::reset_bytecode()
{
	if (original_bytecode_ptr)
	{
		setup_entry_bytecode->bytecode = original_bytecode_ptr;
		original_bytecode_ptr = nullptr;
		bytecode.clear();
	}
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

ProfileInfo* Core::Proc::profile() const
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
Value Core::Proc::call(std::vector<Value> arguments, Value usr)
{
	std::vector<ManagedValue> margs;
	for (Value& v : arguments)
	{
		margs.emplace_back(v);
	}
	return CallGlobalProc(usr.type, usr.value, 2, id, 0, DataType::NULL_D, 0, margs.data(), margs.size(), 0, 0);
}

Disassembly Core::Proc::disassemble()
{
	return Disassembly::from_proc(*this);
}

void Core::Proc::assemble(Disassembly disasm)
{
	std::vector<std::uint32_t> bc = disasm.assemble();
	auto size = bc.size();
	set_bytecode(std::move(bc));
	setup_entry_bytecode->bytecode_length = size;
	//proc_table_entry->local_var_count_idx = Core::get_proc("/proc/twelve_locals").proc_table_entry->local_var_count_idx;
}

Core::Proc& Core::get_proc(std::string name, unsigned int override_id)
{
	strip_proc_path(name);
	//Core::Alert("Attempting to get proc " + name + ", override " + std::to_string(override_id));
	return procs_by_id.at(procs_by_name.at(name).at(override_id));
}

Core::Proc* Core::try_get_proc(std::string name, unsigned int override_id)
{
	strip_proc_path(name);
	if (auto ptr = procs_by_name.find(name); ptr != procs_by_name.end())
	{
		if (override_id < ptr->second.size())
		{
			return &procs_by_id[ptr->second[override_id]];
		}
	}
	return nullptr;
}

Core::Proc& Core::get_proc(unsigned int id)
{
	return procs_by_id.at(id);
}

Core::Proc& Core::get_proc(ExecutionContext* ctx)
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
		Proc p {};
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
			procs_by_name[p.name] = std::vector<unsigned int>();
		}
		p.override_id = procs_by_name.at(p.name).size();
		procs_by_name[p.name].push_back(p.id);
		procs_by_id.push_back(std::move(p));
		i++;
	}
	return true;
}

void Core::destroy_proc_list()
{
	procs_by_id.clear();
	procs_by_name.clear();
}
