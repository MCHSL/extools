#include "../core/core.h"
#include "GasMixture.h"
#include "turf_grid.h"
#include "../dmdism/opcodes.h"

trvh fuck(unsigned int args_len, Value* args, Value src)
{
	return Value("fuck");
}

std::unordered_map<std::string, Value> gas_types;
std::unordered_map<unsigned int, int> gas_ids;
std::unordered_map<unsigned int, std::shared_ptr<GasMixture>> gas_mixtures;
std::vector<Value> gas_id_to_type;
TurfGrid all_turfs;

int str_id_volume;
trvh gasmixture_register(unsigned int args_len, Value* args, Value src)
{
	gas_mixtures[src.value] = std::make_shared<GasMixture>(src.get_by_id(str_id_volume).valuef);
	return Value::Null();
}

trvh gasmixture_unregister(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) {return Value::Null();}
	gas_mixtures.erase(src.value);
	return Value::Null();
}

trvh gasmixture_heat_capacity(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	return Value(gas_mixtures[src.value]->heat_capacity());
}

trvh gasmixture_total_moles(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	return Value(gas_mixtures[src.value]->total_moles());
}

trvh gasmixture_return_pressure(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	return Value(gas_mixtures[src.value]->return_pressure());
}

trvh gasmixture_return_temperature(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	return Value(gas_mixtures[src.value]->get_temperature());
}

trvh gasmixture_return_volume(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	return Value(gas_mixtures[src.value]->get_volume());
}

trvh gasmixture_thermal_energy(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	return Value(gas_mixtures[src.value]->thermal_energy());
}

trvh gasmixture_archive(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	gas_mixtures[src.value]->archive();
	return Value::Null();
}

trvh gasmixture_merge(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	if (args_len < 1)
		return Value::Null();
	gas_mixtures[src.value]->merge(*gas_mixtures[args[0].value]);
	return Value::Null();
}

trvh gasmixture_remove_ratio(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	if (args_len < 2 || args[0].type != DATUM)
		return Value::Null();
	gas_mixtures[args[0].value]->copy_from_mutable(gas_mixtures[src.value]->remove_ratio(args[1].valuef));
	return Value::Null();
}

trvh gasmixture_remove(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	if (args_len < 2 || args[0].type != DATUM)
		return Value::Null();
	gas_mixtures[args[0].value]->copy_from_mutable(gas_mixtures[src.value]->remove(args[1].valuef));
	return Value::Null();
}

trvh gasmixture_copy_from(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	if (args_len < 1 || args[0].type != DATUM)
		return Value::Null();
	gas_mixtures[args[0].value]->copy_from_mutable(*gas_mixtures[src.value]);
	return Value::Null();
}

trvh gasmixture_share(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	if (args_len < 1 || args[0].type != DATUM)
		return Value::Null();
	return Value(gas_mixtures[src.value]->share(*gas_mixtures[args[0].value], args_len >= 2 ? args[1].valuef : 4));
}

trvh gasmixture_get_last_share(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	return Value(gas_mixtures[src.value]->get_last_share());
}

trvh gasmixture_get_gases(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	List l(CreateList(0));
	GasMixture &gm = *gas_mixtures[src.value];
	for (int i = 0; i < TOTAL_NUM_GASES; i++) {
		if (gm.get_moles(i) >= GAS_MIN_MOLES) {
			l.append(gas_id_to_type[i]);
		}
	}
	return l;
}

trvh gasmixture_set_temperature(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	gas_mixtures[src.value]->set_temperature(args_len > 0 ? args[0].valuef : 0);
	return Value::Null();
}

trvh gasmixture_set_volume(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	gas_mixtures[src.value]->set_volume(args_len > 0 ? args[0].valuef : 0);
	return Value::Null();
}

trvh gasmixture_get_moles(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	if (args_len < 1 || args[0].type != DATUM_TYPEPATH)
		return Value::Null();
	int index = gas_ids[args[0].value];
	return Value(gas_mixtures[src.value]->get_moles(index));
	return Value::Null();
}

trvh gasmixture_set_moles(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	if (args_len < 2 || args[0].type != DATUM_TYPEPATH)
		return Value::Null();
	int index = gas_ids[args[0].value];
	gas_mixtures[src.value]->set_moles(index, args[1].valuef);
	return Value::Null();
}

trvh gasmixture_mark_immutable(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	gas_mixtures[src.value]->mark_immutable();
	return Value::Null();
}

trvh gasmixture_clear(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	gas_mixtures[src.value]->clear();
	return Value::Null();
}

trvh gasmixture_compare(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	if (args_len < 1 || args[0].type != DATUM)
		return Value::Null();
	int result = gas_mixtures[src.value]->compare(*gas_mixtures[args[0].value]);
	if (result == -1) {
		return Value("temp");
	}
	else if (result == -2) {
		return Value("");
	} else{
		return gas_id_to_type[result];
	}
}

trvh gasmixture_multiply(unsigned int args_len, Value* args, Value src)
{
	if (gas_mixtures.find(src.value) == gas_mixtures.end()) { return Value::Null(); }
	gas_mixtures[src.value]->multiply(args_len > 0 ? args[0].valuef : 1);
	return Value::Null();
}

trvh turf_update_adjacent(unsigned int args_len, Value* args, Value src)
{
	if (src.type != TURF) { return Value::Null(); }
	Tile *tile = all_turfs.get(src.value);
	if (tile != nullptr) {
		tile->update_adjacent(all_turfs);
	}
	return Value::Null();
}

trvh turf_update_air_ref(unsigned int args_len, Value* args, Value src)
{
	if (src.type != TURF) { return Value::Null(); }
	Tile *tile = all_turfs.get(src.value);
	if (tile != nullptr) {
		tile->update_air_ref();
	}
	return Value::Null();
}

trvh refresh_atmos_grid(unsigned int args_len, Value* args, Value src)
{
	all_turfs.refresh();
	return Value::Null();
}

int str_id_air;
int str_id_atmosadj;
int str_id_is_openturf;
int str_id_x, str_id_y, str_id_z;
int str_id_current_cycle, str_id_archived_cycle, str_id_excited_group, str_id_planetary_atmos, str_id_initial_gas_mix;
int str_id_excited_groups, str_id_excited, str_id_active_turfs;
int str_id_consider_pressure_difference;
ManagedValue SSair = Value::Null();

const char* enable_monstermos()
{
	// get the var IDs for SANIC SPEED
	str_id_air = Core::GetStringId("air", true);
	str_id_atmosadj = Core::GetStringId("atmos_adjacent_turfs", true);
	str_id_volume = Core::GetStringId("initial_volume", true);
	str_id_is_openturf = Core::GetStringId("is_openturf", true);
	str_id_x = Core::GetStringId("x", true);
	str_id_y = Core::GetStringId("y", true);
	str_id_z = Core::GetStringId("z", true);
	str_id_current_cycle = Core::GetStringId("current_cycle", true);
	str_id_archived_cycle = Core::GetStringId("archived_cycle", true);
	str_id_excited_group = Core::GetStringId("excited_group", true);
	str_id_active_turfs = Core::GetStringId("active_turfs", true);
	str_id_excited_groups = Core::GetStringId("excited_groups", true);
	str_id_excited = Core::GetStringId("excited", true);
	str_id_planetary_atmos = Core::GetStringId("planetary_atmos", true);
	str_id_initial_gas_mix = Core::GetStringId("initial_gas_mix", true);

	SSair = Value::Global().get("SSair");
	//Set up gas types map
	std::vector<Value> nullvector = { Value(0.0f) };
	Container gas_types_list = Core::get_proc("/proc/gas_types").call(nullvector);
	int gaslen = gas_types_list.length();
	if (gaslen != TOTAL_NUM_GASES) {
		return "TOTAL_NUM_GASES does not match the number of /datum/gas subtypes!!";
	}
	for (int i = 0; i < gaslen; ++i)
	{
		Value v = gas_types_list.at(i);
		gas_types[Core::stringify(v)] = gas_types_list.at(i);
		gas_ids[v.value] = i;
		gas_specific_heat[i] = gas_types_list.at(v).valuef;
		gas_id_to_type.push_back(v);
	}
	//Set up hooks
	Core::get_proc("/datum/gas_mixture/proc/__gasmixture_register").hook(gasmixture_register);
	Core::get_proc("/datum/gas_mixture/proc/__gasmixture_unregister").hook(gasmixture_unregister);
	Core::get_proc("/datum/gas_mixture/proc/heat_capacity").hook(gasmixture_heat_capacity);
	Core::get_proc("/datum/gas_mixture/proc/total_moles").hook(gasmixture_total_moles);
	Core::get_proc("/datum/gas_mixture/proc/return_pressure").hook(gasmixture_return_pressure);
	Core::get_proc("/datum/gas_mixture/proc/return_temperature").hook(gasmixture_return_temperature);
	Core::get_proc("/datum/gas_mixture/proc/return_volume").hook(gasmixture_return_volume);
	Core::get_proc("/datum/gas_mixture/proc/thermal_energy").hook(gasmixture_thermal_energy);
	Core::get_proc("/datum/gas_mixture/proc/archive").hook(gasmixture_archive);
	Core::get_proc("/datum/gas_mixture/proc/merge").hook(gasmixture_merge);
	Core::get_proc("/datum/gas_mixture/proc/copy_from").hook(gasmixture_copy_from);
	Core::get_proc("/datum/gas_mixture/proc/share").hook(gasmixture_share);
	Core::get_proc("/datum/gas_mixture/proc/compare").hook(gasmixture_compare);
	Core::get_proc("/datum/gas_mixture/proc/get_gases").hook(gasmixture_get_gases);
	Core::get_proc("/datum/gas_mixture/proc/__remove").hook(gasmixture_remove);
	Core::get_proc("/datum/gas_mixture/proc/__remove_ratio").hook(gasmixture_remove_ratio);
	Core::get_proc("/datum/gas_mixture/proc/set_temperature").hook(gasmixture_set_temperature);
	Core::get_proc("/datum/gas_mixture/proc/set_volume").hook(gasmixture_set_volume);
	Core::get_proc("/datum/gas_mixture/proc/get_moles").hook(gasmixture_get_moles);
	Core::get_proc("/datum/gas_mixture/proc/set_moles").hook(gasmixture_set_moles);
	Core::get_proc("/datum/gas_mixture/proc/mark_immutable").hook(gasmixture_mark_immutable);
	Core::get_proc("/datum/gas_mixture/proc/clear").hook(gasmixture_clear);
	Core::get_proc("/datum/gas_mixture/proc/multiply").hook(gasmixture_multiply);
	Core::get_proc("/datum/gas_mixture/proc/get_last_share").hook(gasmixture_get_last_share);
	Core::get_proc("/turf/proc/__update_extools_adjacent_turfs").hook(turf_update_adjacent);
	Core::get_proc("/turf/open/proc/update_air_ref").hook(turf_update_air_ref);
	Core::get_proc("/world/proc/refresh_atmos_grid").hook(refresh_atmos_grid);

	all_turfs.refresh();
	return "ok";
}

extern "C" EXPORT const char* init_monstermos(int a, const char** b)
{
	if (!Core::initialize())
	{
		return "no";
	}
	return enable_monstermos();
}