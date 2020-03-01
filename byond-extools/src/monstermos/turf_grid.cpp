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

extern Value SSair;
std::vector<ExcitedGroup*> excited_groups;

void Tile::process_cell(int fire_count) {
	if (turf_ref.get_by_id(str_id_archived_cycle) < fire_count) {
		archive(fire_count);
	}
	SetVariable(turf_ref.type, turf_ref.value, str_id_current_cycle, Value(float(fire_count)));

	bool has_planetary_atmos = turf_ref.get_by_id(str_id_planetary_atmos).valuef;
	Value our_excited_group = turf_ref.get_by_id(str_id_excited_group);
	int adjacent_turfs_length = 0;
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

		if (our_excited_group.type != NULL_D && enemy_excited_group.type != NULL_D) {
			if (our_excited_group != enemy_excited_group) {

			}
			should_share_air = true;
		}
		else if (air->compare(*enemy_tile.air)) {
			if (true) {
				SSair.invoke("add_to_active", {enemy_tile.turf_ref});
			}
			Value excited_group = our_excited_group;

		}
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

}
void ExcitedGroup::add_turf(Tile &tile) {

}
void ExcitedGroup::self_breakdown(bool space_is_all_consuming = false) {

}
void ExcitedGroup::dismantle(bool unexcite = true) {
	Container active_turfs = SSair.get_by_id(str_id_active_turfs);
	int read_i = 0;
	int at_length = active_turfs.length;
	for (int i = 0; i < active_turfs.length; i++) {
		if (active_turfs.at(i)) {

		}
	}
}
