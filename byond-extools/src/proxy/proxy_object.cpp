#include "proxy_object.h"
#include "../core/byond_structures.h"
#include "../dmdism/opcodes.h"
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
	return oGetVariable(datumType, datumId, name_id);
}

void hSetVariable(int datumType, int datumId, unsigned int name_id, Value new_value)
{
	/*if (setters.find(name_id) != setters.end())
	{
		auto tmp = setters.at(name_id);
		setters.erase(name_id);
		tmp.call({ new_value }, Value::Null(), Value(datumType, datumId));
		setters[name_id] = tmp;
		return;
	}*/
	oSetVariable(datumType, datumId, name_id, new_value);
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

bool Proxy::initialize()
{
	oGetVariable = Core::install_hook(GetVariable, hGetVariable);
	oSetVariable = Core::install_hook(SetVariable, hSetVariable);
	if (false)
	{
		Core::get_proc("/datum/proxy_object/proc/__install").hook(install_proxy);

	}
	return true;
}