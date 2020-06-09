#include "reftracking.h"
#include "../core/byond_structures.h"
#include "../dmdism/opcodes.h"
#include "../third_party/robin_hood.h"
#include <chrono>
#include <atomic>

#include <fstream>
#include <unordered_set>

SetVariablePtr oSetVariable;
AppendToContainerPtr oAppendToContainer;
RemoveFromContainerPtr oRemoveFromContainer;
SetAssocElement1Ptr oSetAssocElement;
InitializeListFromContextPtr oInitializeListFromContext;
DestroyListPtr oDestroyList;
DestroyDatumPtr oDestroyDatum;

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
		current_value.type = (DataType)(current_value.type & 0xFF);
		//DecRefCount(current_value.type, current_value.value);
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

	oSetVariable(datum.type, datum.value, name_id, new_value);
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
	DecRefCount(current_value.type, current_value.value);
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

void hDestroyDatum(int unk1, int unk2, trvh datum)
{
	for (Reference& ref : forward_references[datum.type][datum.value])
	{
		auto& backrefs = back_references[ref.holder.type][ref.holder.value];
		backrefs.erase(std::remove_if(backrefs.begin(), backrefs.end(), [datum](Reference& ref) { return ref.holder == datum; }), backrefs.end());
	}
	forward_references[datum.type][datum.value].clear();
	oDestroyDatum(unk1, unk2, datum);
}

trvh get_backrefs(unsigned int n_args, trvh* args, trvh src)
{
	exporting_refs = true;
	Container result;
	//IncRefCount(result.type, result.id);
	const Value a = args[0];
	for (Reference& ref : back_references[a.type][a.value])
	{
		result[ref.holder] = Value(DataType::STRING, ref.varname);
	}
	exporting_refs = false;
	return result;
}

trvh get_forwardrefs(unsigned int n_args, trvh* args, trvh src)
{
	exporting_refs = true;
	Container result;
	//IncRefCount(result.type, result.id);
	const Value a = args[0];
	for (Reference& ref : forward_references[a.type][a.value])
	{
		result[Value(DataType::STRING, ref.varname)] = ref.holder;
	}
	exporting_refs = false;
	return result;
}


bool RefTracking::initialize()
{
	back_references.clear();
	forward_references.clear();
	back_references[DataType::DATUM].reserve(50000);
	back_references[DataType::AREA].reserve(50000);
	back_references[DataType::TURF].reserve(50000);
	back_references[DataType::OBJ].reserve(50000);
	back_references[DataType::MOB].reserve(50000);

	oSetVariable = Core::install_hook(SetVariable, (SetVariablePtr)hSetVariable);
	oAppendToContainer = Core::install_hook(AppendToContainer, (AppendToContainerPtr)hAppendToContainer);
	oInitializeListFromContext = Core::install_hook(InitializeListFromContext, hInitializeListFromContext);
	oRemoveFromContainer = Core::install_hook(RemoveFromContainer, (RemoveFromContainerPtr)hRemoveFromContainer);
	oSetAssocElement = Core::install_hook(SetAssocElement1, (SetAssocElement1Ptr)hSetAssocElement);
	oDestroyList = Core::install_hook(DestroyList, hDestroyList);
	oDestroyDatum = Core::install_hook(DestroyDatum, hDestroyDatum);

	Core::get_proc("/proc/get_back_references").hook((ProcHook)get_backrefs);
	Core::get_proc("/proc/get_forward_references").hook((ProcHook)get_forwardrefs);

	return true;
}