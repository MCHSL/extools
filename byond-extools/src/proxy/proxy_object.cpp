#include "proxy_object.h"

GetVariablePtr oGetVariable;

Value hGetVariable(Value datum, unsigned int name_id)
{
	return oGetVariable(datum, name_id);
}

bool Proxy::initialize()
{
	oGetVariable = (GetVariablePtr)Core::install_hook(GetVariable, hGetVariable);
	return true;
}