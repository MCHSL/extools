#include "byond_structures.h"
#include "core.h"

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
	return has_var(name) ? get(name) : Value::Null();
}

bool Value::has_var(std::string name)
{
	int name_str_id = Core::GetStringId(name);
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
	return false;
}

void Value::set(std::string name, Value value)
{
	SetVariable(type, value, Core::GetStringId(name), value);
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