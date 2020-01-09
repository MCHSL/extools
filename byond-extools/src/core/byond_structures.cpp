#include "byond_structures.h"
#include "core.h"
#include <algorithm>
#include <vector>

Value::Value(std::string s)
{
	type = 0x06;
	value = Core::GetStringId(s);
}

Value::Value(const char* s)
{
	type = 0x06;
	value = Core::GetStringId(s);
}

Value::Value(Core::ManagedString& ms)
{
	type = 0x06;
	value = ms;
}

Value::operator std::string()
{
	return Core::GetStringFromId(value);
}

Value::operator float()
{
	return valuef;
}

Value Value::get(std::string name)
{
	return GetVariable(type, value, Core::GetStringId(name));
}

Value Value::get_safe(std::string name)
{
	return has_var(name) ? static_cast<trvh>(get(name)) : Value::Null();
}

Value Value::get_by_id(int id)
{
	return GetVariable(type, value, id);
}

std::unordered_map<std::string, Value> Value::get_all_vars()
{
	Container vars = get("vars");
	int len = vars.length();
	std::unordered_map<std::string, Value> vals;
	for (int i = 0; i < len; i++)
	{
		std::string varname = vars.at(i);
		vals[varname] = get(varname);
	}
	return vals;
}

bool Value::has_var(std::string name)
{
	Value v = name;
	Value vars = get("vars");
	return IsInContainer(v.type, v.value, vars.type, vars.value);
	/*int name_str_id = Core::GetStringId(name); //good night sweet prince
	Container contents = get("vars");
	int left = 0;
	int right = contents.length() - 1;
	while (left <= right)
	{
		int mid = (left + right) / 2;
		Value midval = contents.at(mid);
		if (midval.value == name_str_id)
		{
			return true;
		}

		std::string midstr = midval;
		if (midstr.compare(name) < 0)
		{
			left = mid + 1;
		}
		else
		{
			right = mid - 1;
		}
	}
	return false;*/
}

void Value::set(std::string name, Value value)
{
	SetVariable(type, value, Core::GetStringId(name), value);
}

Value Value::invoke(std::string name, std::vector<Value> args, Value usr)
{
	std::replace(name.begin(), name.end(), '_', ' ');
	return CallProcByName(usr.type, usr.value, 2, Core::GetStringId(name), type, value, args.data(), args.size(), 0, 0);
}

Value List::at(int index)
{
	return list->vector_part[index];
}

Value List::at(Value key)
{
	return GetAssocElement(0x0F, id, key.type, key.value);
}

void List::append(Value val)
{
	return AppendToContainer(0x0F, id, val.type, val.value);
}

List::List()
{
	id = CreateList(8);
	list = GetListPointerById(id);
}

List::List(int _id) : id(_id)
{
	list = GetListPointerById(id);
}

List::List(Value v)
{
	id = v.value;
	list = GetListPointerById(id);
}

unsigned int Container::length()
{
	return Length(type, id);
}

Value Container::at(unsigned int index)
{
	return at(Value(index+1));
}

Value Container::at(Value key)
{
	return GetAssocElement(type, id, key.type, key.value);
}

ContainerProxy::operator Value()
{
	return GetAssocElement(c.type, c.id, key.type, key.value);
}

void ContainerProxy::operator=(Value val)
{
	SetAssocElement(c.type, c.id, key.type, key.value, val.type, val.value);
}

std::string BSocket::addr()
{
	return Core::GetStringFromId(addr_string_id);
}