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

void Value::set(std::string name, Value value)
{
	SetVariable(type, value, Core::GetStringId(name), value);
}

Value IDList::at(int index)
{
	return list->vector_part[index];
}

Value IDList::at(Value key)
{
	return GetAssocElement(0x0F, id, key.type, key.value);
}