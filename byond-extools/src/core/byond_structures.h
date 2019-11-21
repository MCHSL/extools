#pragma once

#include <cstdint>

#ifdef _WIN32
#define REGPARM3
#else
#define REGPARM3 __attribute__((regparm(3)))
#endif
#define FLAG_PROFILE 0x10000

struct String
{
	char* stringData;
	int unk1;
	int unk2;
	unsigned int refcount;
};

struct trvh //temporary return value holder, used for sidestepping the fact that structs with constructors are passed in memory and not in eax/ecx when returning them
{
	char type;
	union
	{
		int value;
		float valuef;
	};
};

struct Value
{
	char type;
	union
	{
		int value;
		float valuef;
	};

	Value(char type, int value) : type(type), value(value) {};
	Value(char type, float valuef) : type(type), valuef(valuef) {};
	Value(trvh trvh)
	{
		type = trvh.type;
		if (type == 0x2A)
			value = trvh.value;
		else
			valuef = trvh.valuef;
	}
	inline static Value Null() {
		return { 0, 0 };
	}
};

struct IDArrayEntry
{
	short size;
	int unknown;
	int refcountMaybe;
};

struct List
{
	Value* elements;
	int unk1;
	int unk2;
	int length;
	int refcount;
	int unk3;
	int unk4;
	int unk5;
};

struct Type
{
	unsigned int path;
	unsigned int parentTypeIdx;
	unsigned int last_typepath_part;
};

struct ProcArrayEntry
{
	int procPath;
	int procName;
	int procDesc;
	int procCategory;
	int procFlags;
	int unknown1;
	unsigned short bytecode_idx; // ProcSetupEntry index
	unsigned short local_var_count_idx; // ProcSetupEntry index 
	int unknown2;
};

struct SuspendedProc
{
	char unknown[0x88];
	int time_to_resume;
};

struct ExecutionContext;

struct ProcConstants
{
	int proc_id;
	int unknown2;
	Value src;
	Value usr;
	ExecutionContext* context;
	int unknown3;
	int unknown4; //some callback thing
	union
	{
		int unknown5;
		int extended_profile_id;
	};
	int arg_count;
	Value* args;
};

struct ExecutionContext
{
	ProcConstants* constants;
	ExecutionContext* parent_context;
	std::uint32_t dbg_proc_file;
	std::uint32_t dbg_current_line;
	std::uint32_t* bytecode;
	std::uint16_t current_opcode;
	Value cached_datum;
	char unknown2[8];
	std::uint32_t test_flag;
	char unknown3[12];
	Value* local_variables;
	Value* stack;
	std::uint16_t local_var_count;
	std::uint16_t stack_size;
	std::int32_t unknown; //callback something
	Value* current_iterator;
	std::uint32_t iterator_allocated;
	std::uint32_t iterator_length;
	std::uint32_t iterator_index;
	std::int32_t another_unknown2;
	char unknown4[3];
	char iterator_filtered_type;
	char unknown5;
	char iterator_unknown;
	char unknown6;
	std::uint32_t infinite_loop_count;
	char unknown7[2];
	bool paused;
	char unknown8[51];
};

struct ProcSetupEntry
{
	union
	{
		std::uint16_t local_var_count;
		std::uint16_t bytecode_length;
	};
	std::uint32_t* bytecode;
	std::int32_t unknown;
};

struct ProfileEntry
{
	std::uint32_t seconds;
	std::uint32_t microseconds;

	unsigned long long as_microseconds()
	{
		return 1000000 * (unsigned long long)seconds + microseconds;
	}
	double as_seconds()
	{
		return (double)seconds + microseconds / 1000000;
	}
};

struct ProfileInfo
{
	std::uint32_t call_count;
	ProfileEntry real;
	ProfileEntry total;
	ProfileEntry self;
	ProfileEntry overtime;
	std::uint32_t proc_id;
};