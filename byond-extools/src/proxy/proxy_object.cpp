#include "proxy_object.h"
#include "../core/byond_structures.h"
#include "../dmdism/opcodes.h"
#include "../third_party/robin_hood.h"
#include <chrono>
#include <atomic>

#include <fstream>
#include <unordered_set>

GetVariablePtr oGetVariable;
SetVariablePtr oSetVariable;

std::unordered_map<int, bool> proxies;

std::unordered_map<int, Core::Proc*> getters;
std::unordered_map<int, Core::Proc*> setters;

std::unordered_map<int, std::unordered_map<int, std::unordered_set<unsigned int>>> data_breakpoints;

struct Reference
{
	Value holder;
	unsigned int varname;

	Reference(Value holder, unsigned int varname) : holder(holder), varname(varname) {}
};

std::unordered_map<unsigned int, std::unordered_map<unsigned int, std::vector<Reference>>> back_references;
std::unordered_map<unsigned int, std::unordered_map<unsigned int, std::vector<Reference>>> forward_references;

trvh handle_proxy(int id, int name_id)
{
	std::ifstream fin("wack.txt");
	std::string wack;
	fin >> wack;
	return { DataType::STRING, (int)Core::GetStringId(wack) };
}

trvh hGetVariable(int datumType, int datumId, unsigned int name_id)
{
	/*if (proxies.find(datumId) != proxies.end())
	{
		return handle_proxy(datumId, name_id);
	}
	else if (getters.find(name_id) != getters.end())
	{
		auto tmp = getters.at(name_id);
		getters.erase(name_id);
		auto res = tmp.call({}, Value::Null(), Value(datumType, datumId));
		getters[name_id] = tmp;
		return res;
	}*/
	/*if (data_breakpoints[datumType][datumId].find(name_id) != data_breakpoints[datumType][datumId].end())
	{

	}*/
	static unsigned int back_refs_name_id = Core::GetStringId("back_references");
	static unsigned int fore_refs_name_id = Core::GetStringId("forward_references");
	if (name_id == back_refs_name_id)
	{
		List result;
		for (Reference& ref : back_references[datumType][datumId])
		{
			result.append(ref.holder);
		}
		return result;
	}
	else if (name_id == fore_refs_name_id)
	{
		List result;
		for (Reference& ref : forward_references[datumType][datumId])
		{
			result.append(ref.holder);
		}
		return result;
	}
	return oGetVariable(datumType, datumId, name_id);
}

bool isdatom(trvh v)
{
	return v.type == DataType::DATUM || v.type == DataType::AREA || v.type == DataType::TURF || v.type == DataType::OBJ || v.type == DataType::MOB;
}

void hSetVariable(trvh datum, unsigned int name_id, trvh new_value)
{
	if (isdatom(datum))
	{
		ManagedValue current_value = Value(datum).get_by_id(name_id);
		if (isdatom(current_value))
		{
			auto& backrefs = back_references[current_value.type][current_value.value];
			backrefs.erase(std::remove_if(backrefs.begin(), backrefs.end(), [datum](Reference& ref) { return ref.holder == datum; }), backrefs.end());

			auto& frontrefs = forward_references[datum.type][datum.value];
			frontrefs.erase(std::remove_if(frontrefs.begin(), frontrefs.end(), [current_value](Reference& ref) { return ref.holder == current_value; }), frontrefs.end());
		}
		if (isdatom(new_value))
		{
			back_references[new_value.type][new_value.value].emplace_back(datum, name_id);
			forward_references[datum.type][datum.value].emplace_back(new_value, name_id);
		}
	}
	/*if (setters.find(name_id) != setters.end())
	{
		auto tmp = setters.at(name_id);
		setters.erase(name_id);
		tmp.call({ new_value }, Value::Null(), Value(datumType, datumId));
		setters[name_id] = tmp;
		return;
	}*/
	oSetVariable(datum.type, datum.value, name_id, new_value);
}

trvh install_proxy(unsigned int n_args, Value* args, Value src)
{
	proxies[src.value] = true;
	return Value::True();
}

void add_data_breakpoint(int datumType, int datumId, std::string varName)
{
	data_breakpoints[datumType][datumId].emplace(Core::GetStringId(varName));
}

void remove_data_breakpoint(int datumType, int datumId, std::string varName)
{
	data_breakpoints[datumType][datumId].erase(Core::GetStringId(varName));
}

void on_data_read(int datumId, int datumType, std::string varName)
{

}

void on_data_write(int datumId, int datumType, std::string varName, Value new_value)
{

}

trvh get_backrefs(unsigned int n_args, Value* args, Value src)
{
	List result;
	for (Reference& ref : back_references[src.type][src.value])
	{
		result.append(ref.holder);
	}
	return result;
}

trvh get_forwardrefs(unsigned int n_args, Value* args, Value src)
{
	List result;
	for (Reference& ref : forward_references[src.type][src.value])
	{
		result.append(ref.holder);
	}
	return result;
}

trvh clear_refs(unsigned int n_args, Value* args, Value src)
{
	for (Reference& ref : forward_references[src.type][src.value])
	{
		auto& backrefs = back_references[ref.holder.type][ref.holder.value];
		backrefs.erase(std::remove_if(backrefs.begin(), backrefs.end(), [src](Reference& ref) { return ref.holder == src; }), backrefs.end());
	}
	forward_references[src.type][src.value].clear();
	return Value::Null();
}

bool Proxy::initialize()
{
	oGetVariable = Core::install_hook(GetVariable, hGetVariable);
	oSetVariable = Core::install_hook(SetVariable, (SetVariablePtr)hSetVariable);
	if (false)
	{
		Core::get_proc("/datum/proxy_object/proc/__install").hook(install_proxy);
	}
	Core::get_proc("/datum/proc/get_backrefs").hook(get_backrefs);
	Core::get_proc("/datum/proc/get_forwardrefs").hook(get_forwardrefs);
	Core::get_proc("/datum/proc/clear_refs").hook(clear_refs);
	return true;
}