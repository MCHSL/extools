#pragma once

struct String
{
	char* stringData;
	int unk1;
	int unk2;
	unsigned int refcount;
};

struct Value
{
	char type;
	union
	{
		int value;
		float valuef;
	};
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
	int bytecode_idx; // ProcSetupEntry index
	int local_var_count_idx; // ProcSetupEntry index 
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
	int unknown4;
	int unknown5;
	int arg_count;
	Value* args;
};

struct ExecutionContext
{
	ProcConstants* constants;
	ExecutionContext* parent_context;
	int dbg_proc_file;
	int dbg_current_line;
	int* bytecode;
	unsigned short current_opcode;
	Value cached_datum;
	char unknown2[8];
	int test_flag;
	char unknown3[12];
	Value* local_variables;
	Value* stack;
	short local_var_count;
	short stack_size;
	int unknown;
	Value* current_iterator;
	int iterator_allocated;
	int iterator_length;
	int iterator_index;
	char unknown4[7];
	char iterator_filtered_type;
	char unknown5;
	char iterator_unknown;
	char unknown6;
	int infinite_loop_count;
	char unknown7[2];
	bool paused;
	char unknown8[51];
};

struct ProcSetupEntry
{
	union
	{
		short local_var_count;
		short bytecode_length;
	};
	int* bytecode;
	int unknown;
};