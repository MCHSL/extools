
#include "helpers.h"

std::string byond_tostring(int idx)
{
	String* s = GetStringTableEntry(idx);
	return s ? std::string(s->stringData) : "";
}

std::string tohex(int numero) {
	std::stringstream stream;
	stream << std::hex << std::uppercase << numero;
	return "0x" + std::string(stream.str());
}

std::string todec(int numero) {
	std::stringstream stream;
	stream << std::dec << numero;
	return std::string(stream.str());
}
