#pragma once

#include <cstdint>
#include <cstddef>
namespace Pocket
{
	namespace Sigscan
	{
		bool DataCompare(const unsigned char* base, const char* pattern);
		void* FindPattern(std::uintptr_t address, size_t size, const char* pattern, short offset = 0);
		void* FindPattern(const char* moduleName, const char* pattern, short offset = 0);
	}
}