#include "byond_structures.h"
#include "core.h"
#include <algorithm>
#include <vector>
#include <cassert>

Value::Value(std::string s)
{
	type = DataType::STRING;
	value = Core::GetStringId(s);
}

Value::Value(const char* s)
{
	type = DataType::STRING;
	value = Core::GetStringId(s);
}

Value::Value(Core::ManagedString& ms)
{
	type = DataType::STRING;
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

static bool to_bool(Value& v)
{
	switch (v.type)
	{
		case DataType::NULL_D:
			return false;
		case DataType::NUMBER:
			return v.valuef != 0.0f;
		case DataType::STRING:
			return *(GetStringTableEntry(v.value)->stringData) != 0;
		default:
			return v.value != 0xFFFF;
	}
}

Value::operator void*() //if you attempt to delete a value I will eat you
{
	return (void*) to_bool(*this);
}

ManagedValue Value::get(std::string name)
{
	return GetVariable(type, value, Core::GetStringId(name));
}

ManagedValue Value::get_safe(std::string name)
{
	return has_var(name) ? static_cast<trvh>(get(name)) : Value::Null();
}

ManagedValue Value::get_by_id(int id)
{
	return GetVariable(type, value, id);
}

std::unordered_map<std::string, Value> Value::get_all_vars()
{
	Container vars = *this == Global() ? Value { DataType::LIST_GLOBAL_VARS, 0 } : get("vars");
	int len = vars.length();
	std::unordered_map<std::string, Value> vals;
	for (int i = 0; i < len; i++)
	{
		const std::string& varname = vars.at(i);
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

void Value::set(std::string name, Value newvalue)
{
	SetVariable(type, value, Core::GetStringId(name), newvalue);
}

ManagedValue Value::invoke(std::string name, std::vector<Value> args, Value usr)
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

ManagedValue Value::invoke_by_id(int id, std::vector<Value> args, Value usr)
{
	std::vector<ManagedValue> margs;
	for (Value& v : args)
	{
		margs.emplace_back(v);
	}
	return CallProcByName(usr.type, usr.value, 2, id, type, value, margs.data(), margs.size(), 0, 0);
}

Value& Value::operator+=(const Value& rhs)
{
	if (type == DataType::NUMBER && rhs.type == DataType::NUMBER)
		valuef += rhs.valuef;
	else if (type == DataType::STRING && rhs.type == DataType::STRING)
		value = Core::GetStringId(Core::GetStringFromId(value) + Core::GetStringFromId(rhs.value));
	else
	{
		assert(false);
		//Runtime("Attempt to add invalid types in native code! (good luck)");
	}
	return *this;
}

Value& Value::operator-=(const Value& rhs)
{
	if (type == DataType::NUMBER && rhs.type == DataType::NUMBER)
		valuef -= rhs.valuef;
	else
	{
		assert(false);
		//Runtime("Attempt to subtract invalid types in native code! (good luck)");
	}
	return *this;
}

Value& Value::operator*=(const Value& rhs)
{
	if (type == DataType::NUMBER && rhs.type == DataType::NUMBER)
		valuef *= rhs.valuef;
	else
	{
		assert(false);
		//Runtime("Attempt to multiply invalid types in native code! (good luck)");
	}
	return *this;
}

Value& Value::operator/=(const Value& rhs)
{
	if (type == DataType::NUMBER && rhs.type == DataType::NUMBER)
		valuef /= rhs.valuef;
	else
	{
		assert(false);
		//Runtime("Attempt to divide invalid types in native code! (good luck)");
	}
	return *this;
}

#define VALOPS(ret, l, r, op) inline ret operator op(l lhs, r rhs) { return lhs op##= rhs; }
#define ALLVALOPS(ret, l, r) VALOPS(ret, l, r, +); VALOPS(ret, l, r, -); VALOPS(ret, l, r, *); VALOPS(ret, l, r, /);

ALLVALOPS(Value, Value, Value&)
ALLVALOPS(Value, Value, float)
ALLVALOPS(Value, Value, std::string)

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

Container::Container(DataType type, int id) : type(type), id(id)
{
	IncRefCount(type, id);
}

Container::Container(Value val) : type(val.type), id(val.value)
{
	IncRefCount(type, id);
}

Container::~Container()
{
	DecRefCount(type, id);
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
	//IncRefCount(type, value);
	other.type = DataType::NULL_D;
	other.value = 0;
}

ManagedValue& ManagedValue::operator =(const ManagedValue& other)
{
	if (&other == this) return *this;
	DecRefCount(type, value);
	type = other.type;
	value = other.value;
	IncRefCount(type, value);
	return *this;
}

ManagedValue& ManagedValue::operator =(ManagedValue&& other) noexcept
{
	if (&other == this) return *this;
	type = other.type;
	value = other.value;
	other.type = DataType::NULL_D;
	other.value = 0;
	return *this;
}

ManagedValue::~ManagedValue()
{
	DecRefCount(type, value);
}