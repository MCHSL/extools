#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "byond_constants.h"

#ifdef _WIN32
#define REGPARM3
#define REGPARM2
#else
#define REGPARM3 __attribute__((regparm(3)))
#define REGPARM2 __attribute__((regparm(2)))
#endif
#define FLAG_PROFILE 0x10000

#define PROC_FLAG_HIDDEN 1

struct String
{
	char* stringData;
	int unk1;
	int unk2;
	unsigned int refcount;
};

struct trvh //temporary return value holder, used for sidestepping the fact that structs with constructors are passed in memory and not in eax/ecx when returning them
{
	DataType type;
	union
	{
		int value;
		float valuef;
	};
};

namespace Core
{
	struct ManagedString;
}

struct ManagedValue;


struct Value
{
	DataType type;
	union
	{
		int value;
		float valuef;
	};
	Value() { type = DataType::NULL_D; value = 0; }
	Value(DataType type, int value) : type(type), value(value) {};
	Value(trvh trvh)
	{
		type = trvh.type;
		if (type == DataType::NUMBER)
			value = trvh.value;
		else
			valuef = trvh.valuef;
	}
	Value(float valuef) : type(DataType::NUMBER), valuef(valuef) {};
	Value(std::string s);
	Value(const char* s);
	Value(Core::ManagedString& ms);


	inline static trvh Null() {
		return { DataType::NULL_D, 0 };
	}

	inline static trvh True()
	{
		trvh t { DataType::NUMBER };
		t.valuef = 1.0f;
		return t;
	}

	inline static trvh False()
	{
		return { DataType::NUMBER, 0 };
	}

	inline static Value Global()
	{
		return { DataType::WORLD_D, 0x01 };
	}

	inline static Value World()
	{
		return { DataType::WORLD_D, 0x00 };
	}

	/* inline static Value Tralse()
	{
		return { 0x2A, rand() % 1 };
	} */

	operator trvh()
	{
		return trvh{ type, value };
	}

	bool operator==(const Value& rhs)
	{
		return value == rhs.value && type == rhs.type;
	}

	bool operator!=(const Value& rhs)
	{
		return !(*this == rhs);
	}

	Value& operator +=(const Value& rhs);
	Value& operator -=(const Value& rhs);
	Value& operator *=(const Value& rhs);
	Value& operator /=(const Value& rhs);
	/*inline Value& operator +=(float rhs);
	inline Value& operator -=(float rhs);
	inline Value& operator *=(float rhs);
	inline Value& operator /=(float rhs);*/

	operator std::string();
	operator float();
	operator void*();
	ManagedValue get(std::string name);
	ManagedValue get_safe(std::string name);
	ManagedValue get_by_id(int id);
	ManagedValue invoke(std::string name, std::vector<Value> args, Value usr = Value::Null());
	ManagedValue invoke_by_id(int id, std::vector<Value> args, Value usr = Value::Null());
	std::unordered_map<std::string, Value> get_all_vars();
	bool has_var(std::string name);
	void set(std::string name, Value value);
};

struct ManagedValue : Value
{
	//This class is used to prevent objects being garbage collected before you are done with them
	ManagedValue() = delete;
	ManagedValue(Value val);
	ManagedValue(DataType type, int value);
	ManagedValue(trvh trvh);
	ManagedValue(std::string s);
	ManagedValue(const ManagedValue& other);
	ManagedValue(ManagedValue&& other) noexcept;
	ManagedValue& operator =(const ManagedValue& other);
	ManagedValue& operator =(ManagedValue&& other) noexcept;
	~ManagedValue();
};

struct IDArrayEntry
{
	short size;
	int unknown;
	int refcountMaybe;
};

enum class RbtColor : bool
{
	Black = false,
	Red = true,
};

struct AssociativeListEntry
{
	Value key;
	Value value;
	RbtColor color;
	AssociativeListEntry* left;
	AssociativeListEntry* right;
};

struct RawList
{
	Value* vector_part;
	AssociativeListEntry* map_part;
	int allocated_size; //maybe
	int length;
	int refcount;
	int unk3; //this one appears to be a pointer to a struct holding the vector_part pointer, a zero, and maybe the initial size? no clue.

	bool is_assoc()
	{
		return map_part != nullptr;
	}

};

struct DatumVarEntry
{
	int fuck;
	unsigned int id;
	Value value;
};

struct RawDatum
{
	int type_id;
	DatumVarEntry *vars;
	short len_vars; // maybe
	short fuck;
	int cunt;
	int refcount;
};

struct Container;

struct ContainerProxy
{
	Container& c;
	Value key = Value::Null();
	ContainerProxy(Container& c, Value key) : c(c), key(key) {}

	operator Value();
	void operator=(Value val);
};

struct Container //All kinds of lists, including magical snowflake lists like contents
{
	Container();
	Container(DataType type, int id);
	Container(Value val);
	~Container();
	DataType type;
	int id;

	Value at(unsigned int index);
	Value at(Value key);

	unsigned int length();

	operator Value()
	{
		return { type, id };
	}

	operator trvh()
	{
		return { type, id };
	}

	ContainerProxy operator[](unsigned int index)
	{
		return ContainerProxy(*this, Value((float)(index + 1)));
	}

	ContainerProxy operator[](Value key)
	{
		return ContainerProxy(*this, key);
	}
};

struct List //Specialization for Container with fast access by index
{
	List();
	List(int _id);
	List(Value v);
	~List();
	RawList* list;

	int id;

	Value at(int index);
	Value at(Value key);
	void append(Value val);

	bool is_assoc()
	{
		return list->is_assoc();
	}

	Value* begin() { return list->vector_part; }
	Value* end() { return list->vector_part + list->length; }

	operator trvh()
	{
		return { DataType::LIST, id };
	}

	operator Container()
	{
		return { DataType::LIST, id };
	}
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
	int params_idx;
};

struct ExecutionContext;

struct ProcConstants
{
	int proc_id;
	int flags;
	Value usr;
	Value src;
	ExecutionContext* context;
	int sequence_number;
	int unknown4; //some callback thing
	union
	{
		int unknown5;
		int extended_profile_id;
	};
	int arg_count;
	Value* args;
	char unknown6[88];
	int time_to_resume;
};

typedef ProcConstants SuspendedProc;

struct ExecutionContext
{
	ProcConstants* constants;
	ExecutionContext* parent_context;
	std::uint32_t dbg_proc_file;
	std::uint32_t dbg_current_line;
	std::uint32_t* bytecode;
	std::uint16_t current_opcode;
	char test_flag;
	char unknown1;
	Value cached_datum;
	Value unknown2;
	char unknown3[8];
	Value dot;
	Value* local_variables;
	Value* stack;
	std::uint16_t local_var_count;
	std::uint16_t stack_size;
	void* unknown4;
	Value* current_iterator;
	std::uint32_t iterator_allocated;
	std::uint32_t iterator_length;
	std::uint32_t iterator_index;
	Value iterator_filtered;
	char unknown5;
	char iterator_unknown;
	char unknown6;
	std::uint32_t infinite_loop_count;
	char unknown7[2];
	bool paused;
	char unknown8[51];
};

struct ProcBytecode
{
	std::uint16_t length;
	std::uint32_t** ppBytecode;
};

struct BytecodeEntry_V1
{
	std::uint16_t bytecode_length;
	std::uint32_t* bytecode;
};

struct BytecodeEntry_V2
{
	std::uint16_t bytecode_length;
	std::uint32_t unknown;
	std::uint32_t* bytecode;
};

struct LocalVars
{
	std::uint16_t count;
	std::uint32_t* var_name_indices;
};

struct LocalVarsEntry_V1
{
	std::uint16_t count;
	std::uint32_t* var_name_indices;
};

struct LocalVarsEntry_V2
{
	std::uint16_t count;
	std::uint32_t unknown;
	std::uint32_t* var_name_indices;
};

struct ParamsData
{
	uint32_t unk_0;
	uint32_t unk_1;
	uint32_t name_index;
	uint32_t unk_2;
};

struct Params
{
	std::uint16_t count;
	ParamsData* params;
};

struct ParamsEntry_V1
{
	std::uint16_t params_count_mul_4;
	ParamsData* params;

	std::uint16_t count()
	{
		return params_count_mul_4 / 4;
	}
};

struct ParamsEntry_V2
{
	std::uint16_t params_count_mul_4;
	std::uint32_t unknown;
	ParamsData* params;

	std::uint16_t count()
	{
		return params_count_mul_4 / 4;
	}
};

struct MiscEntry
{
private:
	union
	{
		ParamsEntry_V1 parameters1;
		ParamsEntry_V2 parameters2;
		LocalVarsEntry_V1 local_vars1;
		LocalVarsEntry_V2 local_vars2;
		BytecodeEntry_V1 bytecode1;
		BytecodeEntry_V2 bytecode2;
	};

public:
	Params as_params();
	LocalVars as_locals();
	ProcBytecode as_bytecode();
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
		return (double)seconds + ((double)microseconds / 1000000);
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

struct NetMsg //named after the struct ThreadedNetMsg - unsure if it's actually that struct
{
	std::uint32_t type;
	std::uint32_t payload_length;
	std::uint32_t unk1;
	std::uint32_t unk2;
	char* payload;
	std::uint32_t unk3;
	std::uint32_t raw_header;
};

struct BSocket //or client?
{
	std::uint32_t unk1;
	std::uint32_t addr_string_id;
	//more unknown fields here
	//EAX + 0x444 is the refcount, holy crap!
	//EAX + 0x54 - key/username
	std::string addr();
};

struct Hellspawn
{
	std::uint32_t outta_my_way;
	std::uint32_t shove_off;
	std::uint32_t handle;
};

struct TableHolderThingy
{
	unsigned int length;
	unsigned int* elements; //?????
};

struct TableHolder2
{
	void** elements;
	unsigned int length;
};

struct VarListEntry;

struct UnknownSimpleLinkedListEntry
{
	unsigned int value;
	UnknownSimpleLinkedListEntry* next;
};

struct UnknownComplexLinkedListEntry
{
	char unknown[0x34];
	UnknownComplexLinkedListEntry* next;
	char unknown2[8];
};

struct TableHolder3
{
	void* elements;
	std::uint32_t size;
	std::uint32_t capacity;
	TableHolder3* next; //probably?
	char unknown[8];
};

struct Obj
{
	trvh loc;
	char unknown[28];
	UnknownComplexLinkedListEntry* some_other_linked_list;
	VarListEntry* modified_vars;
	std::uint16_t modified_vars_count;
	std::uint16_t modified_vars_capacity;
	char unknown2[12];
	UnknownSimpleLinkedListEntry* some_linked_list;
	TableHolder3* vis_contents;
	TableHolder3* vis_locs;
	char unknown3[84];
};

struct Datum
{
	std::uint32_t type;
	VarListEntry* modified_vars;
	std::uint16_t modifier_vars_count;
	std::uint16_t modified_vars_capacity;
	std::uint32_t flags;
	std::uint32_t refcount;
};

struct Mob
{
	char unknown1[0x24];
	UnknownComplexLinkedListEntry* unknown_list1;
	VarListEntry* modified_vars;
	std::uint16_t modified_vars_count;
	std::uint16_t modified_vars_capacity;
	char unknown2[0xA];
	UnknownSimpleLinkedListEntry* unknown_list2;
	TableHolder3* some_holder1;
	TableHolder3* some_holder2;
	char unknown3[0x60];
	UnknownSimpleLinkedListEntry* unknown_list3;
	char unknown4[0x10];
};

struct VarListEntry
{
	std::uint32_t unknown;
	std::uint32_t name_id;
	trvh value;
};

struct SuspendedProcList
{
	ProcConstants** buffer;
	std::uint32_t front;
	std::uint32_t back;
	std::uint32_t max_elements;
};