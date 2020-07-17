#include "byond_structures.h"
#include "core.h"
#include <algorithm>
#include <vector>
#include <cassert>

Value::Value(const std::string& s)
{
	type = DataType::STRING;
	value = Core::GetStringId(s);
}

/*Value::Value(const char* s)
{
	type = DataType::STRING;
	value = Core::GetStringId(s);
}*/

Value::Value(Core::ManagedString& ms)
{
	type = DataType::STRING;
	value = ms;
}

Value Value::String(const std::string& s)
{
	trvh t{ DataType::STRING };
	t.value = Core::GetStringId(s);
	return t;
}

Value::operator std::string() const
{
	return Core::GetStringFromId(value);
}

Value::operator float() const
{
	return valuef;
}

Value::operator void*() const //if you attempt to delete a value I will eat you
{
	return (void*)((type == 0x2A && valuef != 0.0f) || (type == 0x06 && *(GetStringTableEntry(value)->stringData) != 0) || (type != 0 && value != 0xFFFF));
}

ManagedValue Value::get(const std::string& name) const
{
	return GetVariable(type, value, Core::GetStringId(name));
}

ManagedValue Value::get_safe(const std::string& name) const
{
	return has_var(name) ? static_cast<trvh>(get(name)) : Value::Null();
}

ManagedValue Value::get_by_id(const int id) const
{
	return GetVariable(type, value, id);
}

std::unordered_map<std::string, Value> Value::get_all_vars() const
{
	const Container vars = *this == Global() ? Value { DataType::LIST_GLOBAL_VARS, 0 } : get("vars");
	const int len = vars.length();
	std::unordered_map<std::string, Value> vals;
	for (int i = 0; i < len; i++)
	{
		const std::string& varname = vars.at(i);
		vals[varname] = get(varname);
	}
	return vals;
}

bool Value::has_var(const std::string& name) const
{
	const Value v = Value::String(name);
	const Value vars = get("vars");
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

void Value::set(const std::string& name, Value newvalue) const
{
	SetVariable(type, value, Core::GetStringId(name), newvalue);
}

ManagedValue Value::invoke(std::string name, std::vector<Value> args, Value usr) const
{
	std::replace(name.begin(), name.end(), '_', ' ');
	std::vector<ManagedValue> margs;
	for (Value v : args)
	{
		margs.emplace_back(v);
		IncRefCount(v.type, v.value);
	}
	return CallProcByName(usr.type, usr.value, 2, Core::GetStringId(name), type, value, args.data(), args.size(), 0, 0);
}

Value List::at(int index) const
{
	return list->vector_part[index];
}

Value List::at(Value key) const
{
	return GetAssocElement(0x0F, id, key.type, key.value);
}

void List::append(Value val)
{
	return AppendToContainer(0x0F, id, val.type, val.value);
}

List::List()
{
	id = CreateList(0);
	list = GetListPointerById(id);
	IncRefCount(0x0F, id);
}

List::List(int _id) : id(_id)
{
	list = GetListPointerById(id);
	if (!list) {
		throw "Invalid list id";
	}
	IncRefCount(0x0F, id);
}

List::List(Value v)
{
	id = v.value;
	list = GetListPointerById(id);
	IncRefCount(0x0F, id);
}

List::~List()
{
	DecRefCount(0x0F, id);
}

Container::Container()
{
	type = DataType::LIST;
	id = CreateList(0);
	IncRefCount(0x0F, id);
}

Container::Container(const DataType type, const int id) : type(type), id(id)
{
	IncRefCount(type, id);
}

Container::Container(const Value val) : type(val.type), id(val.value)
{
	IncRefCount(type, id);
}

Container::~Container()
{
	DecRefCount(type, id);
}

unsigned int Container::length() const
{
	return Length(type, id);
}

Value Container::at(unsigned int index) const
{
	return at(Value::Number(index+1));
}

Value Container::at(Value key) const
{
	return GetAssocElement(type, id, key.type, key.value);
}

ContainerProxy::operator Value() const
{
	return GetAssocElement(c.type, c.id, key.type, key.value);
}

void ContainerProxy::operator=(Value val) const
{
	SetAssocElement(c.type, c.id, key.type, key.value, val.type, val.value);
}

std::string BSocket::addr()
{
	return Core::GetStringFromId(addr_string_id);
}

ManagedValue::ManagedValue(Value val)
{
	type = val.type;
	value = val.value;
	IncRefCount(type, value);
}

ManagedValue::ManagedValue(DataType type, int value)
{
	type = type;
	value = value;
	IncRefCount(type, value);
}

ManagedValue::ManagedValue(trvh trvh)
{
	type = trvh.type;
	value = trvh.value;
	IncRefCount(type, value);
}

ManagedValue::ManagedValue(std::string s)
{
	type = DataType::STRING;
	value = Core::GetStringId(s);
	IncRefCount(type, value);
}

ManagedValue::ManagedValue(const ManagedValue& other)
{
	type = other.type;
	value = other.value;
	IncRefCount(type, value);
}

ManagedValue::ManagedValue(ManagedValue&& other) noexcept
{
	type = other.type;
	value = other.value;
	IncRefCount(type, value);
}

ManagedValue::~ManagedValue()
{
	DecRefCount(type, value);
}