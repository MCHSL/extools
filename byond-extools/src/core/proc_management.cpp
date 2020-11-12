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
	if (auto proc_pos = name.find("/proc/"); proc_pos != std::string::npos)
	{
		name.erase(proc_pos, 5);
	}
	else if (auto verb_pos = name.find("/verb/"); verb_pos != std::string::npos)
	{
		name.erase(verb_pos, 5);
	}
}

void Core::Proc::set_bytecode(std::vector<std::uint32_t>&& new_bytecode)
{
	if (!original_bytecode_ptr)
	{
		original_bytecode_ptr = bytecode_entry->bytecode;
	}

	bytecode = std::move(new_bytecode);
	bytecode_entry->bytecode = bytecode.data();
}

void Core::Proc::reset_bytecode()
{
	if (original_bytecode_ptr)
	{
		bytecode_entry->bytecode = original_bytecode_ptr;
		original_bytecode_ptr = nullptr;
		bytecode.clear();
	}
}

std::uint32_t* Core::Proc::get_bytecode()
{
	return bytecode_entry->bytecode;
}

std::uint16_t Core::Proc::get_bytecode_length()
{
	return bytecode_entry->bytecode_length;
}

std::uint32_t Core::Proc::get_local_count()
{
	return locals_entry->count;
}

std::string Core::Proc::get_local_name(std::uint32_t index)
{
	if (index >= locals_entry->count)
		return nullptr;

	return GetStringFromId(name_table[locals_entry->var_name_indices[index]]);
}

std::uint32_t Core::Proc::get_param_count()
{
	return params_entry->count();
}

std::string Core::Proc::get_param_name(std::uint32_t index)
{
	if (index >= params_entry->count())
		return nullptr;

	return GetStringFromId(name_table[params_entry->params[index].name_index]);
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
	bytecode_entry->bytecode_length = size;
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
		strip_proc_path(p.name);
		p.simple_name = p.name.substr(p.name.rfind("/") + 1);
		p.proc_table_entry = entry;
		p.bytecode_entry = &misc_entry_table[entry->bytecode_idx]->bytecode;
		p.locals_entry = &misc_entry_table[entry->local_var_count_idx]->local_vars;
		p.params_entry = &misc_entry_table[entry->params_idx]->parameters;
		p.bytecode_idx = entry->bytecode_idx;
		p.varcount_idx = entry->local_var_count_idx;
		auto& procs_by_name_entry = procs_by_name[p.name];
		p.override_id = procs_by_name_entry.size();
		procs_by_name_entry.push_back(p.id);
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
