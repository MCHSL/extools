#include "proxy_object.h"
#include "../core/byond_structures.h"
#include "../dmdism/opcodes.h"


#include <fstream>

GetVariablePtr oGetVariable;
SetVariablePtr oSetVariable;

std::unordered_map<int, bool> proxies;

std::unordered_map<int, Core::Proc> getters;
std::unordered_map<int, Core::Proc> setters;

trvh handle_proxy(int id, int name_id)
{
	std::ifstream fin("wack.txt");
	std::string wack;
	fin >> wack;
	return { DataType::STRING, (int)Core::GetStringId(wack.c_str()) };
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
	return oGetVariable(datumType, datumId, name_id);
}

void hSetVariable(int datumType, int datumId, unsigned int name_id, Value new_value)
{
	if (setters.find(name_id) != setters.end())
	{	
		auto tmp = setters.at(name_id);
		setters.erase(name_id);
		tmp.call({ new_value }, Value::Null(), Value(datumType, datumId));
		setters[name_id] = tmp;
		return;
	}
	oSetVariable(datumType, datumId, name_id, new_value);
}

trvh install_proxy(unsigned int n_args, Value* args, Value src)
{
	proxies[src.value] = true;
	return Value::True();
}

trvh install_accessors(unsigned int n_args, Value* args, Value src)
{
	std::string varname = GetStringTableEntry(args[0].value)->stringData;
	getters[args[0].value] = Core::get_proc("/obj/accessor_thingy/proc/get_" + varname);
	setters[args[0].value] = Core::get_proc("/obj/accessor_thingy/proc/set_" + varname);
	return Value::True();
}

trvh sunshine(unsigned int n_args, Value* args, Value src)
{
	Value t = Core::get_turf(50, 50, 2);
	Core::Alert("The turf at (50, 50, 2) is called *drumroll*: " + std::string(t.get("name")));
	return {};
}

bool Proxy::initialize()
{
	oGetVariable = (GetVariablePtr)Core::install_hook((void*)GetVariable, (void*)hGetVariable);
	oSetVariable = (SetVariablePtr)Core::install_hook((void*)SetVariable, (void*)hSetVariable);
	if (false)
	{
		Core::get_proc("/datum/proxy_object/proc/__install").hook(install_proxy);
		Core::get_proc("/obj/accessor_thingy/proc/__install_accessors").hook(install_accessors);
		Core::get_proc("/client/verb/sacrifice_child").hook(sunshine);
	}
	
	return true;
}