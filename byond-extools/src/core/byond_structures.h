#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#define REGPARM3
#else
#define REGPARM3 __attribute__((regparm(3)))
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
	Value() { type = 0; value = 0; }
	Value(char type, int value) : type(type), value(value) {};
	Value(trvh trvh)
	{
		type = trvh.type;
		if (type == 0x2A)
			value = trvh.value;
		else
			valuef = trvh.valuef;
	}
	Value(float valuef) : type(0x2A), valuef(valuef) {};
	Value(std::string s);
	Value(const char* s);


	inline static Value Null() {
		return { 0, 0 };
	}

	inline static Value True()
	{
		return Value(1.0f); //number
	}

	inline static Value False()
	{
		return Value(0.0f);
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

	operator std::string();
	operator float();
	Value get(std::string name);
	Value get_safe(std::string name);
	Value get_by_id(int id);
	std::unordered_map<std::string, Value> get_all_vars();
	bool has_var(std::string name);
	void set(std::string name, Value value);
};

struct IDArrayEntry
{
	short size;
	int unknown;
	int refcountMaybe;
};

struct AssociativeListEntry
{
	Value key;
	Value value;
	bool red_black; //0 - black
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

	Container(char type, int id) : type(type), id(id) {}
	Container(Value val) : type(val.type), id(val.value) {}
	char type;
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
		return { 0x0F, id };
	}

	operator Container()
	{
		return { 0x0F, id };
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
	Value usr;
	Value src;
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
	char test_flag;
	char unknown1;
	Value cached_datum;
	char unknown2[16];
	Value dot;
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