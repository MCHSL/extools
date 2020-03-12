#include "turf_grid.h"
#include "../dmdism/opcodes.h"
#include "var_ids.h"

extern std::unordered_map<unsigned int, std::shared_ptr<GasMixture>> gas_mixtures;

Tile::Tile()
{
    //ctor
}

void Tile::update_adjacent(TurfGrid &grid) {
	Container adjacent_list = turf_ref.get_by_id(str_id_atmosadj);
	adjacent_bits = 0;
	int adjacent_len = adjacent_list.length();
	for (int i = 0; i < adjacent_len; i++) {
		Value turf = adjacent_list.at(i);
		if (turf.type != TURF)
			continue;
		int x = turf.get_by_id(str_id_x).valuef;
		int y = turf.get_by_id(str_id_y).valuef;
		int z = turf.get_by_id(str_id_z).valuef;
		Tile *other = grid.get(x, y, z);
		if (other == nullptr)
			continue;
		int dir = adjacent_list.at(turf);
		adjacent_bits |= dir;
		switch (dir) {
		case 1:
			adjacent[0] = other;
			break;
		case 2:
			adjacent[1] = other;
			break;
		case 4:
			adjacent[2] = other;
			break;
		case 8:
			adjacent[3] = other;
			break;
		case 16:
			adjacent[4] = other;
			break;
		case 32:
			adjacent[5] = other;
			break;
		}
	}
}

void Tile::update_air_ref() {
	bool isopenturf = turf_ref.get_by_id(str_id_is_openturf).valuef;
	if (isopenturf) {
		Value air_ref = turf_ref.get_by_id(str_id_air);
		if (air_ref.type == DATUM) {
			air = gas_mixtures[air_ref.value];
		}
		else {
			air.reset();
		}
	}
	else {
		air.reset();
	}
}

extern ManagedValue SSair;
std::vector<ExcitedGroup*> excited_groups;

void Tile::process_cell(int fire_count) {
	if (turf_ref.get_by_id(str_id_archived_cycle) < fire_count) {
		archive(fire_count);
	}
	SetVariable(turf_ref.type, turf_ref.value, str_id_current_cycle, Value(float(fire_count)));

	bool has_planetary_atmos = turf_ref.get_by_id(str_id_planetary_atmos).valuef;
	int adjacent_turfs_length = 0;
	atmos_cooldown++;
	for (int i = 0; i < 6; i++) {
		if (adjacent_bits & (1 << i)) adjacent_turfs_length++;
	}
	if (has_planetary_atmos) {
		adjacent_turfs_length++;
	}
	for (int i = 0; i < 6; i++) {
		if (!(adjacent_bits & (1 << i))) continue;
		Tile &enemy_tile = *adjacent[i];
		if (fire_count <= enemy_tile.turf_ref.get_by_id(str_id_current_cycle)) continue;
		enemy_tile.archive(fire_count);

		bool should_share_air = false;
		Value enemy_excited_group = enemy_tile.turf_ref.get_by_id(str_id_excited_group);

		if (excited_group && enemy_tile.excited_group) {
			if (excited_group != enemy_tile.excited_group) {

			}
			should_share_air = true;
		}
		else if (air->compare(*enemy_tile.air)) {
			if (!enemy_tile.excited) {
				SSair.invoke("add_to_active", {enemy_tile.turf_ref});
			}
			std::shared_ptr<ExcitedGroup> eg = excited_group;
			if(!eg)
				eg = enemy_tile.excited_group;
			if (!eg)
				eg = std::make_shared<ExcitedGroup>();
			if (!excited_group)
				ExcitedGroup::add_turf(eg, *this);
			if (!enemy_tile.excited_group)
				ExcitedGroup::add_turf(eg, enemy_tile);
			should_share_air = true;
		}

		if (should_share_air) {
			float difference = air->share(*enemy_tile.air, adjacent_turfs_length);
			if (difference > 0) {
				turf_ref.invoke("consider_pressure_difference", { enemy_tile.turf_ref, difference });
			}
			else {
				enemy_tile.turf_ref.invoke("consider_pressure_difference", { turf_ref, -difference });
			}
			last_share_check();
		}

		if (has_planetary_atmos) {
			if (!planet_atmos_info || planet_atmos_info->last_initial != turf_ref.get_by_id(str_id_initial_gas_mix)) {
				if (!planet_atmos_info) planet_atmos_info = std::make_unique<PlanetAtmosInfo>();
				planet_atmos_info->last_initial = turf_ref.get_by_id(str_id_initial_gas_mix);
				GasMixture air_backup = *air;
				turf_ref.get_by_id(str_id_air).invoke("copy_from_turf", {turf_ref});
				planet_atmos_info->last_mix = *gas_mixtures[turf_ref.get_by_id(str_id_air)];
				planet_atmos_info->last_mix.archive();
				planet_atmos_info->last_mix.mark_immutable();
				*air = air_backup;
			}
			if (air->compare(planet_atmos_info->last_mix)) {
				if (!excited_group) {
					std::shared_ptr<ExcitedGroup> eg = std::make_shared<ExcitedGroup>();
					ExcitedGroup::add_turf(eg, *this);
				}
				air->share(planet_atmos_info->last_mix, adjacent_turfs_length);
				last_share_check();
			}
		}
		turf_ref.get_by_id(str_id_air).invoke("react", {});
		turf_ref.invoke("update_visuals", {});
		if (atmos_cooldown > (EXCITED_GROUP_DISMANTLE_CYCLES * 2)) {
			SSair.invoke("remove_from_active", { turf_ref });
		}
	}
}

void Tile::last_share_check() {
	float last_share = air->get_last_share();
	if (last_share > MINIMUM_AIR_TO_SUSPEND) {
		excited_group->reset_cooldowns();
		atmos_cooldown = 0;
	}
	else if (last_share > MINIMUM_MOLES_DELTA_TO_MOVE) {
		excited_group->dismantle_cooldown = 0;
		atmos_cooldown = 0;
	}
}

void Tile::archive(int fire_count) {
	if (turf_ref.get_by_id(str_id_is_openturf).valuef) {
		SetVariable(turf_ref.type, turf_ref.value, str_id_archived_cycle, Value(float(fire_count)));
	}
	if (air) {
		air->archive();
	}
}

Tile *TurfGrid::get(int x, int y, int z) const {
	if (x < 1 || y < 1 || z < 1 || x > maxx || y > maxy || z > maxz) return nullptr;
	if (!tiles) return nullptr;
	return &tiles[(x - 1) + maxx * (y - 1 + maxy * (z - 1))];
}
Tile *TurfGrid::get(int id) const {
	if (!tiles || id < 0 || id >= maxid) {
		return nullptr;
	}
	return &tiles[id];
}
void TurfGrid::refresh() {
	int new_maxx = Value::World().get("maxx").valuef;
	int new_maxy = Value::World().get("maxy").valuef;
	int new_maxz = Value::World().get("maxz").valuef;
	// we make a new thingy
	// delete the old one too I guess
	std::unique_ptr<Tile[]> new_tiles(new Tile[maxx*maxy*maxz]);

	// make the thingy have actual like values or some shit I guess
	for (int z = 1; z <= maxz; z++) {
		for (int y = 1; y <= maxy; y++) {
			for (int x = 1; x <= maxx; x++) {
				int index = (x - 1) + maxx * (y - 1 + maxy * (z - 1));
				Tile &tile = tiles[index];
				if (x <= maxx && y <= maxy && z <= maxz) {
					tile = *get(x,y,z);
					tile.excited_group.reset(); // excited group contains hanging pointers now (well they're not hanging yet, but they *will* be!)
				}
				tile.turf_ref = Value(TURF, index);
				tile.update_air_ref();
			}
		}
	}

	tiles = std::move(new_tiles);
	maxx = new_maxx; 
	maxy = new_maxy;
	maxz = new_maxz;

	maxid = maxx * maxy * maxz;
	for (int i = 0; i < maxid; i++) {
		tiles[i].update_adjacent(*this);
	}
}

ExcitedGroup::ExcitedGroup() {

}
ExcitedGroup::~ExcitedGroup() {

}
void ExcitedGroup::reset_cooldowns() {
	breakdown_cooldown = 0;
	dismantle_cooldown = 0;
}
void ExcitedGroup::merge_groups(std::shared_ptr<ExcitedGroup> &us, std::shared_ptr<ExcitedGroup> &other) {
	int us_size = us->turf_list.size();
	int other_size = other->turf_list.size();
	if (us_size > other_size) {
		for (int i = 0; i < other_size; i++) {
			Tile *tile = other->turf_list[i];
			tile->excited_group = us;
			us->turf_list.push_back(tile);
		}
		other->turf_list.clear();
		us->reset_cooldowns();
	}
	else {
		for (int i = 0; i < us_size; i++) {
			Tile* tile = us->turf_list[i];
			tile->excited_group = other;
			other->turf_list.push_back(tile);
		}
		us->turf_list.clear();
		other->reset_cooldowns();
	}

}
void ExcitedGroup::add_turf(std::shared_ptr<ExcitedGroup>& us, Tile &tile) {
	us->turf_list.push_back(&tile);
	tile.excited_group = us;
	us->reset_cooldowns();
}
void ExcitedGroup::self_breakdown(bool space_is_all_consuming) {
	GasMixture combined(CELL_VOLUME);

	int turf_list_size = turf_list.size();
	for (int i = 0; i < turf_list_size; i++) {
		Tile& tile = *turf_list[i];
		combined.merge(*tile.air);
	}
	combined.multiply(1 / turf_list_size);
	for (int i = 0; i < turf_list_size; i++){
		Tile &tile = *turf_list[i];
		tile.air->copy_from_mutable(combined);
		tile.atmos_cooldown = 0;
		tile.turf_ref.invoke("update_visuals", {});
	}
	breakdown_cooldown = 0;
}
void ExcitedGroup::dismantle(bool unexcite) {
	Value active_turfs = SSair.get_by_id(str_id_active_turfs);
	int turf_list_size = turf_list.size();
	for (int i = 0; i < turf_list_size; i++) {
		Tile* tile = turf_list[i];
		tile->excited_group.reset();
		if (unexcite) {
			tile->excited = false;
		}
	}
	if (unexcite) {
		std::vector<Value> turf_refs;
		for (int i = 0; i < turf_list_size;  i++) {
			turf_refs.push_back(turf_list[i]->turf_ref);
		}
		active_turfs.invoke("Remove", turf_refs);
	}
	turf_list.clear();
}
