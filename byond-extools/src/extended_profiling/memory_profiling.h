#pragma once
#include "../core/byond_structures.h"
#include <unordered_map>

#define OBJ_BASE_MEM_USAGE sizeof(Obj)
#define DATUM_BASE_MEM_USAGE sizeof(Datum)
#define LIST_BASE_MEM_USAGE sizeof(RawList)
#define MOB_BASE_MEM_USAGE sizeof(Mob)

struct GenericMemUsage
{
	unsigned int base = 0;
	virtual unsigned int total() const = 0;

};

struct ObjMemUsage : GenericMemUsage
{
	unsigned int base = OBJ_BASE_MEM_USAGE;
	unsigned int modified_vars = 0;
	unsigned int vis_contents = 0;
	unsigned int vis_locs = 0;
	unsigned int unknown_simple_linked_list = 0;
	unsigned int unknown_complex_linked_list = 0;

	unsigned int total() const
	{
		return base + modified_vars + vis_contents + vis_locs + unknown_complex_linked_list + unknown_simple_linked_list;
	}
};

struct DatumMemUsage : GenericMemUsage
{
	unsigned int base = DATUM_BASE_MEM_USAGE;
	unsigned int modified_vars = 0;

	unsigned int total() const
	{
		return base + modified_vars;
	}
};

struct ListMemUsage : GenericMemUsage
{
	unsigned int base = LIST_BASE_MEM_USAGE;
	unsigned int vector_part = 0;
	unsigned int map_part = 0;

	inline unsigned int total() const
	{
		return base + vector_part + map_part;
	}
};

struct MobMemUsage : GenericMemUsage
{
	unsigned int base = MOB_BASE_MEM_USAGE;
	unsigned int unknown_linked_list_1 = 0;
	unsigned int unknown_linked_list_2 = 0;
	unsigned int unknown_linked_list_3 = 0;
	unsigned int unknown_table_1 = 0;
	unsigned int unknown_table_2 = 0;
	unsigned int modified_vars = 0;

	unsigned int total() const
	{
		return base + unknown_linked_list_1 + unknown_linked_list_2 + unknown_linked_list_3 + unknown_table_1 + unknown_table_2 + modified_vars;
	}
};

struct BaseUsagePerType
{
	unsigned int base = 0;
	unsigned int instances = 0;
};

struct ObjMemUsagePerType : ObjMemUsage, BaseUsagePerType //is this composition + inheritance?
{
	using BaseUsagePerType::base;
	unsigned int total() const //unfortunately, `using ObjMemUsage::total` doesn't work here
	{
		return base + modified_vars + vis_contents + vis_locs + unknown_complex_linked_list + unknown_simple_linked_list;
	}
};

struct DatumMemUsagePerType : DatumMemUsage, BaseUsagePerType
{
	using BaseUsagePerType::base;

	unsigned int total() const
	{
		return base + modified_vars;
	}
};

struct MobMemUsagePerType : MobMemUsage, BaseUsagePerType
{
	using BaseUsagePerType::base;

	unsigned int total() const
	{
		return base + unknown_linked_list_1 + unknown_linked_list_2 + unknown_linked_list_3 + unknown_table_1 + unknown_table_2 + modified_vars;
	}
};

template<typename EntryType>
struct FullMemUsage
{
	std::unordered_map<std::string, EntryType> entries;
	unsigned int total_instances = 0;
	unsigned int table = 0;
	unsigned int total = 0;
};

struct FullListMemUsage
{
	std::vector<ListMemUsage> entries;
	unsigned int total_instances = 0;
	unsigned int table = 0;
	unsigned int total = 0;
};

ObjMemUsage get_obj_mem_usage(Value obj);
DatumMemUsage get_datum_mem_usage(Value datum);
ListMemUsage get_list_mem_usage(Value list);
MobMemUsage get_mob_mem_usage(Value mob);

FullMemUsage<ObjMemUsagePerType> get_full_obj_mem_usage();
FullMemUsage<DatumMemUsagePerType> get_full_datum_mem_usage();
FullListMemUsage get_full_list_mem_usage();
FullMemUsage<MobMemUsagePerType> get_full_mob_mem_usage();

void dump_full_obj_mem_usage(const std::string& fname);