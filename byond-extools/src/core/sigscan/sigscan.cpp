#include "sigscan.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
// lol
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <link.h>
#include <cstring>
#include <cstdio>

#endif
#define INRANGE(x,a,b)	(x >= a && x <= b) 
#define getBits( x )	(INRANGE((x&(~0x20)),'A','F') ? ((x&(~0x20)) - 'A' + 0xa) : (INRANGE(x,'0','9') ? x - '0' : 0))
#define getByte( x )	(getBits(x[0]) << 4 | getBits(x[1]))

inline bool Pocket::Sigscan::DataCompare(const unsigned char* base, const char* pattern)
{
	for (; *(pattern + 2); ++base, pattern += *(pattern + 1) == ' ' ? 2 : 3)
	{
		if (*pattern != '?')
			if (*base != getByte(pattern))
				return false;
	}

	return *(pattern + 2) == 0;
}

void* Pocket::Sigscan::FindPattern(std::uintptr_t address, size_t size, const char* pattern, const short offset)
{
	for (size_t i = 0; i < size; ++i, ++address)
		if (DataCompare(reinterpret_cast<const unsigned char*>(address), pattern))
			return reinterpret_cast<void*>(address + offset);

	return nullptr;
}
#ifndef _WIN32
static void* disgusting;
size_t disgustingSz;
static int
callback(struct dl_phdr_info* info, size_t size, void* data)
{
	int j;
	//printf("name: %s vs %s\n", info->dlpi_name, (const char*)data);
	if (!strstr(info->dlpi_name, (const char*)data)) return 0;
	for (j = 0; j < info->dlpi_phnum; j++)
	{

		if (info->dlpi_phdr[j].p_type == PT_LOAD)
		{
			char* beg = (char*)info->dlpi_addr + info->dlpi_phdr[j].p_vaddr;
			char* end = beg + info->dlpi_phdr[j].p_memsz;
			disgusting = beg;
			disgustingSz = info->dlpi_phdr[j].p_memsz;
			return 0;
		}
	}
	return 0;
}
#endif
void* Pocket::Sigscan::FindPattern(const char* moduleName, const char* pattern, const short offset)
{

	size_t rangeStart;
	size_t size;
#ifdef _WIN32
	if (!(rangeStart = reinterpret_cast<DWORD>(GetModuleHandleA(moduleName))))
		return nullptr;
	MODULEINFO miModInfo; GetModuleInformation(GetCurrentProcess(), reinterpret_cast<HMODULE>(rangeStart), &miModInfo, sizeof(MODULEINFO));
	size = miModInfo.SizeOfImage;
#else

	disgusting = nullptr;
	dl_iterate_phdr(callback, (void*)moduleName);
	if (!disgusting)
	{
		return nullptr;
	}

	rangeStart = reinterpret_cast<size_t>(disgusting);
	size = disgustingSz;
#endif
	return FindPattern(rangeStart, size, pattern, offset);
}