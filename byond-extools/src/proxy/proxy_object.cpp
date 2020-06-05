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
AppendToContainerPtr oAppendToContainer;
RemoveFromContainerPtr oRemoveFromContainer;
SetAssocElementPtr oSetAssocElement;
InitializeListFromContextPtr oInitializeListFromContext;
DestroyListPtr oDestroyList;

std::unordered_map<int, bool> proxies;

std::unordered_map<int, Core::Proc*> getters;
std::unordered_map<int, Core::Proc*> setters;

std::unordered_map<int, std::unordered_map<int, std::unordered_set<unsigned int>>> data_breakpoints;

bool exporting_refs = false;

struct Reference
{
	Value holder;
	unsigned int varname;

	Reference(Value holder, unsigned int varname) : holder(holder), varname(varname) {}
};

robin_hood::unordered_flat_map<unsigned int, robin_hood::unordered_flat_map<unsigned int, std::vector<Reference>>> back_references;
robin_hood::unordered_flat_map<unsigned int, robin_hood::unordered_flat_map<unsigned int, std::vector<Reference>>> forward_references;

//std::unordered_map<unsigned int, std::vector<Reference>> list_back_references;
//std::unordered_map<unsigned int, std::vector<Reference>> list_forward_references;

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

	static unsigned int indirect_back_refs_name_id = Core::GetStringId("indirect_back_references");
	static unsigned int indirect_fore_refs_name_id = Core::GetStringId("indirect_forward_references");

	/*if (name_id == back_refs_name_id) //nooooooo
	{
		exporting_refs = true;
		Container result;
		for (Reference& ref : back_references[datumType & 0xFF][datumId])
		{
			//Core::Alert("Adding ref");
			result[ref.holder] = Value(DataType::STRING, ref.varname);
			//result.append(ref.holder.value);
			//DecRefCount(ref.holder.type, ref.holder.value);
		}
		exporting_refs = false;
		return result;
	}
	else if (name_id == fore_refs_name_id)
	{
		exporting_refs = true;
		Container result;
		for (Reference& ref : forward_references[datumType & 0xFF][datumId])
		{
			result[Value(DataType::STRING, ref.varname)] = ref.holder;
			//result.append(ref.holder);
			//DecRefCount(ref.holder.type, ref.holder.value);
		}
		exporting_refs = false;
		return result;
	}*/
	return oGetVariable(datumType, datumId, name_id);
}

bool isdatom(const trvh v)
{
	return v.type == DataType::DATUM || v.type == DataType::AREA || v.type == DataType::TURF || v.type == DataType::OBJ || v.type == DataType::MOB;
}

bool islist(const trvh v)
{
	return v.type == DataType::LIST;
}

void hSetVariable(trvh datum, unsigned int name_id, trvh new_value)
{
	new_value.type = (DataType)(new_value.type & 0xFF);
	if (isdatom(datum))
	{
		Value current_value = Value(datum).get_by_id(name_id);
		//DecRefCount(current_value.type, current_value.value);
		current_value.type = (DataType)(current_value.type & 0xFF);
		if (isdatom(current_value) || islist(current_value))
		{
			auto& backrefs = back_references[current_value.type][current_value.value];
			backrefs.erase(std::remove_if(backrefs.begin(), backrefs.end(), [&datum, name_id](Reference& ref) { return ref.holder == datum && ref.varname == name_id; }), backrefs.end());

			auto& frontrefs = forward_references[datum.type][datum.value];
			frontrefs.erase(std::remove_if(frontrefs.begin(), frontrefs.end(), [&current_value, name_id](Reference& ref) { return ref.holder == current_value && ref.varname == name_id; }), frontrefs.end());
		}
		if (isdatom(new_value) || islist(new_value))
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

trvh get_backrefs(unsigned int n_args, trvh* args, trvh src)
{
	exporting_refs = true;
	Container result;
	IncRefCount(result.type, result.id);
	const Value a = args[0];
	for (Reference& ref : back_references[a.type][a.value])
	{
		result[ref.holder] = Value(DataType::STRING, ref.varname);
		//result.append(ref.holder);
	}
	exporting_refs = false;
	return result;
}

trvh get_forwardrefs(unsigned int n_args, trvh* args, trvh src)
{
	exporting_refs = true;
	Container result;
	IncRefCount(result.type, result.id);
	const Value a = args[0];
	for (Reference& ref : forward_references[a.type][a.value])
	{
		result[Value(DataType::STRING, ref.varname)] = ref.holder;
		//result.append(ref.holder);
	}
	exporting_refs = false;
	return result;
}

trvh clear_refs(unsigned char n_args, trvh* args, trvh src)
{
	trvh a = args[0];
	for (Reference& ref : forward_references[a.type][a.value])
	{
		auto& backrefs = back_references[ref.holder.type][ref.holder.value];
		backrefs.erase(std::remove_if(backrefs.begin(), backrefs.end(), [&a](Reference& ref) { return ref.holder == a; }), backrefs.end());
	}
	forward_references[a.type][a.value].clear();
	return Value::Null();
}

void hAppendToContainer(trvh container, trvh value)
{
	if (exporting_refs)
	{
		oAppendToContainer(container.type, container.value, value.type, value.value);
		return;
	}
	container.type = (DataType)(container.type & 0xFF);
	value.type = (DataType)(value.type & 0xFF);
	if (container.type == DataType::LIST)
	{
		if (isdatom(value) || islist(value))
		{
			forward_references[container.type][container.value].emplace_back(value, 0);
			back_references[value.type][value.value].emplace_back(container, 0);
		}
	}
	oAppendToContainer(container.type, container.value, value.type, value.value);
}

bool hRemoveFromContainer(trvh container, trvh value)
{
	container.type = (DataType)(container.type & 0xFF);
	value.type = (DataType)(value.type & 0xFF);
	if (container.type == DataType::LIST)
	{
		if (isdatom(value) || islist(value))
		{
			auto& backrefs = back_references[value.type][value.value];
			backrefs.erase(std::remove_if(backrefs.begin(), backrefs.end(), [&container](Reference& ref) { return ref.holder == container; }), backrefs.end());

			auto& frontrefs = forward_references[container.type][container.value];
			frontrefs.erase(std::remove_if(frontrefs.begin(), frontrefs.end(), [&value](Reference& ref) { return ref.holder == value; }), frontrefs.end());
		}
	}
	return oRemoveFromContainer(container.type, container.value, value.type, value.value);
}

void hSetAssocElement(trvh container, trvh key, trvh value)
{
	if (exporting_refs)
	{
		oSetAssocElement(container.type, container.value, key.type, key.value, value.type, value.value);
		return;
	}
	container.type = (DataType)(container.type & 0xFF);
	key.type = (DataType)(key.type & 0xFF);
	value.type = (DataType)(value.type & 0xFF);
	Value current_value = GetAssocElement(container.type, container.value, key.type, key.value); //ugly
	//DecRefCount(current_value.type, current_value.value);
	if (isdatom(current_value) || islist(current_value))
	{
		auto& backrefs = back_references[current_value.type][current_value.value];
		backrefs.erase(std::remove_if(backrefs.begin(), backrefs.end(), [container](Reference& ref) { return ref.holder == container; }), backrefs.end());

		auto& frontrefs = forward_references[container.type][container.value];
		frontrefs.erase(std::remove_if(frontrefs.begin(), frontrefs.end(), [current_value](Reference& ref) { return ref.holder == current_value; }), frontrefs.end());
	}
	if (isdatom(value) || islist(value))
	{
		back_references[value.type][value.value].emplace_back(container, 0);
		forward_references[container.type][container.value].emplace_back(value, 0);
	}
	oSetAssocElement(container.type, container.value, key.type, key.value, value.type, value.value);
}

trvh hInitializeListFromContext(unsigned int list_id)
{
	trvh result = oInitializeListFromContext(list_id);
	if(exporting_refs)
	{
		return result;
	}
	for (Value& value : List(list_id))
	{
		if (isdatom(value) || islist(value))
		{
			forward_references[DataType::LIST][list_id].emplace_back(value, 0);
			back_references[value.type][value.value].emplace_back(trvh{ DataType::LIST, (int)list_id }, 0);
		}
	}
	return result;
}

void hDestroyList(unsigned int list_id)
{
	for (Reference& ref : forward_references[DataType::LIST][list_id])
	{
		auto& backrefs = back_references[ref.holder.type][ref.holder.value];
		backrefs.erase(std::remove_if(backrefs.begin(), backrefs.end(), [list_id](Reference& ref) { return ref.holder == Value(DataType::LIST, list_id); }), backrefs.end());
	}
	forward_references[DataType::LIST][list_id].clear();
	oDestroyList(list_id);
}

bool Proxy::initialize()
{
	back_references.clear();
	forward_references.clear();
	back_references[DataType::DATUM].reserve(50000);
	back_references[DataType::AREA].reserve(50000);
	back_references[DataType::TURF].reserve(50000);
	back_references[DataType::OBJ].reserve(50000);
	back_references[DataType::MOB].reserve(50000);
	oGetVariable = Core::install_hook(GetVariable, hGetVariable);
	oSetVariable = Core::install_hook(SetVariable, (SetVariablePtr)hSetVariable);
	oAppendToContainer = Core::install_hook(AppendToContainer, (AppendToContainerPtr)hAppendToContainer);
	oInitializeListFromContext = Core::install_hook(InitializeListFromContext, hInitializeListFromContext);
	oRemoveFromContainer = Core::install_hook(RemoveFromContainer, (RemoveFromContainerPtr)hRemoveFromContainer);
	oSetAssocElement = Core::install_hook(SetAssocElement, (SetAssocElementPtr)hSetAssocElement);
	oDestroyList = Core::install_hook(DestroyList, hDestroyList);
	if (false)
	{
		Core::get_proc("/datum/proxy_object/proc/__install").hook(install_proxy);
	}
	Core::get_proc("/proc/get_back_references").hook((ProcHook)get_backrefs);
	Core::get_proc("/proc/get_forward_references").hook((ProcHook)get_forwardrefs);
	Core::get_proc("/proc/clear_references").hook((ProcHook)clear_refs);
	return true;
}