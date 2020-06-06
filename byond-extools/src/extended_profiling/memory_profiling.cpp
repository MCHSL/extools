#include "../third_party/json.hpp"
#include "../core/core.h"
#include "memory_profiling.h"
#include <fstream>
#include <iomanip>

template<typename T>
unsigned int calc_linked_list(T* ptr)
{
	unsigned int ret = 0;
	while (ptr)
	{
		ret += sizeof(T);
		ptr = ptr->next;
	}
	return ret;
}

ObjMemUsage get_obj_mem_usage(const Value obj)
{
	ObjMemUsage result;
	Obj* entry = (Obj*)Core::obj_table->elements[obj.value];
	if (!entry)
	{
		result.base = 0;
		return result;
	}
	if (entry->modified_vars)
	{
		result.modified_vars = entry->modified_vars_capacity * sizeof(VarListEntry);
	}
	if (entry->vis_contents)
	{
		result.vis_contents = entry->vis_contents->capacity * sizeof(Value);
	}
	if (entry->vis_locs)
	{
		result.vis_locs = entry->vis_locs->capacity * sizeof(Value);
	}

	result.unknown_simple_linked_list = calc_linked_list<UnknownSimpleLinkedListEntry>(entry->some_linked_list);
	result.unknown_complex_linked_list = calc_linked_list<UnknownComplexLinkedListEntry>(entry->some_other_linked_list);

	return result;
}

DatumMemUsage get_datum_mem_usage(Value datum)
{
	DatumMemUsage result;
	Datum* entry = (Datum*)Core::datum_table->elements[datum.value];
	if (!entry)
	{
		result.base = 0;
		return result;
	}
	if (entry->modified_vars)
	{
		result.modified_vars = entry->modified_vars_capacity * sizeof(VarListEntry);
	}
	return result;
}

ListMemUsage get_list_mem_usage(Value list)
{
	ListMemUsage result;
	RawList* entry = (RawList*)Core::list_table->elements[list.value];
	if (!entry)
	{
		result.base = 0;
		return result;
	}
	result.vector_part = entry->length * sizeof(Value);
	result.map_part = GetRBTreeMemoryUsage(entry->map_part);
	return result;
}

MobMemUsage get_mob_mem_usage(Value mob)
{
	MobMemUsage result;
	Mob* entry = (Mob*)Core::mob_table->elements[mob.value];
	if (!entry)
	{
		result.base = 0;
		return result;
	}
	if (entry->modified_vars)
	{
		result.modified_vars = entry->modified_vars_capacity * sizeof(VarListEntry);
	}
	result.unknown_linked_list_1 = calc_linked_list<UnknownComplexLinkedListEntry>(entry->unknown_list1);
	result.unknown_linked_list_2 = calc_linked_list<UnknownSimpleLinkedListEntry>(entry->unknown_list2);
	result.unknown_linked_list_3 = calc_linked_list<UnknownSimpleLinkedListEntry>(entry->unknown_list3);
	if(entry->some_holder1)
		result.unknown_table_1 = entry->some_holder1->capacity * sizeof(Value);
	if(entry->some_holder2)
		result.unknown_table_2 = entry->some_holder2->capacity * sizeof(Value);

	return result;
}

FullMemUsage<ObjMemUsagePerType> get_full_obj_mem_usage()
{
	FullMemUsage<ObjMemUsagePerType> result;
	for (unsigned int i = 0; i < Core::obj_table->length; i++)
	{
		Value obj(DataType::OBJ, i);
		ObjMemUsage usage = get_obj_mem_usage(obj);
		if (!usage.base)
		{
			continue;
		}
		std::string type = Core::stringify(obj.get("type"));
		result.total_instances++;
		result.total += usage.total();
		auto& u = result.entries[type];
		u.instances++;
		u.base += usage.base;
		u.modified_vars += usage.modified_vars;
		u.unknown_complex_linked_list += usage.unknown_complex_linked_list;
		u.unknown_simple_linked_list += usage.unknown_simple_linked_list;
		u.vis_contents += usage.vis_contents;
		u.vis_locs += usage.vis_locs; //hate cpp
	}
	result.table = Core::obj_table->length * sizeof(Obj*);
	result.total += result.table; //bad design!!!!!!!!!!! fix this later you idiot
	return result;
}

FullMemUsage<DatumMemUsagePerType> get_full_datum_mem_usage()
{
	FullMemUsage<DatumMemUsagePerType> result;
	for (unsigned int i = 0; i < Core::datum_table->length; i++)
	{
		Value datum(DataType::DATUM, i);
		DatumMemUsage usage = get_datum_mem_usage(datum);
		if (!usage.base)
		{
			continue;
		}
		std::string type = Core::stringify(datum.get("type"));
		result.total_instances++;
		result.total += usage.total();
		auto& u = result.entries[type];
		u.base += usage.base;
		u.instances++;
		u.modified_vars += usage.modified_vars;
	}
	result.table = Core::datum_table->length * sizeof(Datum*);
	result.total += result.table;
	return result;
}

FullListMemUsage get_full_list_mem_usage()
{
	FullListMemUsage result;

	for (unsigned int i = 0; i < Core::list_table->length; i++)
	{
		Value list(DataType::LIST, i);
		ListMemUsage usage = get_list_mem_usage(list);
		if (!usage.base)
		{
			continue;
		}
		result.entries.push_back(usage);
		result.total += usage.total();
		result.total_instances++;
	}
	result.table = Core::list_table->length * sizeof(RawList*);
	result.total += result.table;
	return result;
}

FullMemUsage<MobMemUsagePerType> get_full_mob_mem_usage()
{
	FullMemUsage<MobMemUsagePerType> result;
	for (unsigned int i = 0; i < Core::mob_table->length; i++)
	{
		Value mob(DataType::MOB, i);
		MobMemUsage usage = get_mob_mem_usage(mob);
		if (!usage.base)
		{
			continue;
		}
		std::string type = Core::stringify(mob.get("type"));
		result.total_instances++;
		result.total += usage.total();
		auto& u = result.entries[type];
		u.base += usage.base;
		u.instances++;
		u.modified_vars += usage.modified_vars;
		u.unknown_linked_list_1 += usage.unknown_linked_list_1;
		u.unknown_linked_list_2 += usage.unknown_linked_list_2;
		u.unknown_linked_list_3 += usage.unknown_linked_list_3;
		u.unknown_table_1 += usage.unknown_table_1;
		u.unknown_table_2 += usage.unknown_table_2;
	}
	result.table = Core::mob_table->length * sizeof(Mob*);
	result.total += result.table;
	return result;
}

void to_json(nlohmann::json& j, const ObjMemUsage& omu)
{
	j = nlohmann::json{ {"base", omu.base},
		{"modified_vars", omu.modified_vars},
		{"simple_linked_list", omu.unknown_simple_linked_list},
		{"complex_linked_list", omu.unknown_complex_linked_list},
		{"vis_locs", omu.vis_locs},
		{"vis_contents", omu.vis_contents},
		{"total", omu.total()}
	};
}

void to_json(nlohmann::json& j, const ObjMemUsagePerType& omu)
{
	j = nlohmann::json{ {"base", omu.base},
		{"modified_vars", omu.modified_vars},
		{"simple_linked_list", omu.unknown_simple_linked_list},
		{"complex_linked_list", omu.unknown_complex_linked_list},
		{"vis_locs", omu.vis_locs},
		{"vis_contents", omu.vis_contents},
		{"instances", omu.instances},
		{"total", omu.total()}
	};
}

void to_json(nlohmann::json& j, const ListMemUsage& lmu)
{
	j = nlohmann::json{ {"base", lmu.base},
		{"vector_part", lmu.vector_part},
		{"map_part", lmu.map_part},
		{"total", lmu.total()}
	};
}

void to_json(nlohmann::json& j, const MobMemUsage& omu)
{
	j = nlohmann::json{ {"base", omu.base},
		{"modified_vars", omu.modified_vars},
		{"unknown_linked_list_1", omu.unknown_linked_list_1},
		{"unknown_linked_list_2", omu.unknown_linked_list_2},
		{"unknown_linked_list_3", omu.unknown_linked_list_3},
		{"unknown_table_1", omu.unknown_table_1},
		{"unknown_table_2", omu.unknown_table_2},
		{"total", omu.total()}
	};
}

void to_json(nlohmann::json& j, const MobMemUsagePerType& omu)
{
	j = nlohmann::json{ {"base", omu.base},
		{"modified_vars", omu.modified_vars},
		{"unknown_linked_list_1", omu.unknown_linked_list_1},
		{"unknown_linked_list_2", omu.unknown_linked_list_2},
		{"unknown_linked_list_3", omu.unknown_linked_list_3},
		{"unknown_table_1", omu.unknown_table_1},
		{"unknown_table_2", omu.unknown_table_2},
		{"instances", omu.instances},
		{"total", omu.total()}
	};
}

template<typename T>
void to_json(nlohmann::json& j, const FullMemUsage<T>& fmu)
{
	j = nlohmann::json{ {"entries", fmu.entries},
		{"total_instances", fmu.total_instances},
		{"table", fmu.table},
		{"total", fmu.total}
	};
}

void to_json(nlohmann::json& j, const FullListMemUsage& fmu)
{
	j = nlohmann::json{ {"entries", fmu.entries},
		{"total_instances", fmu.total_instances},
		{"table", fmu.table},
		{"total", fmu.total}
	};
}

void to_json(nlohmann::json& j, const DatumMemUsage& dmu)
{
	j = nlohmann::json{ {"base", dmu.base},
		{"modified_vars", dmu.modified_vars},
		{"total", dmu.total()}
	};
}

void to_json(nlohmann::json& j, const DatumMemUsagePerType& dmu)
{
	j = nlohmann::json{ {"base", dmu.base},
		{"modified_vars", dmu.modified_vars},
		{"instances", dmu.instances},
		{"total", dmu.total()}
	};
}

void dump_full_obj_mem_usage(const std::string& fname)
{
	nlohmann::json objs = get_full_obj_mem_usage();
	nlohmann::json datums = get_full_datum_mem_usage();
	nlohmann::json mobs = get_full_mob_mem_usage();
	nlohmann::json everything;
	everything["obj"] = objs;
	everything["datum"] = datums;
	everything["mobs"] = mobs;
	everything["total"] = objs["total"].get<unsigned int>() + datums["total"].get<unsigned int>() + mobs["total"].get<unsigned int>();
	everything["total_instances"] = objs["total_instances"].get<unsigned int>() + datums["total_instances"].get<unsigned int>() + mobs["total_instances"].get<unsigned int>();
	std::ofstream o(fname);
	o << std::setw(2) << everything;
}