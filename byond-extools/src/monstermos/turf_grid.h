#pragma once

#include "GasMixture.h"
#include "../core/core.h"
#include <memory>

class TurfGrid;
struct PlanetAtmosInfo;
struct ExcitedGroup;
struct MonstermosInfo;

extern std::vector<std::weak_ptr<ExcitedGroup>> excited_groups;

struct Tile
{
    Tile();
	void update_air_ref();
	void update_adjacent(TurfGrid &grid);
	void process_cell(int fire_count);
	void archive(int fire_count);
	void last_share_check();
	void update_planet_atmos();
	void adjust_eq_movement(int dir, float amount);
	void finalize_eq();
	void finalize_eq_neighbors(float *transfer_dirs);
	void equalize_pressure_in_zone(int cyclenum);
	void explosively_depressurize(int cyclenum);
	// adjacent tiles in the order NORTH,SOUTH,EAST,WEST,UP,DOWN.
	// 
	Tile *adjacent[6];
	unsigned char adjacent_bits = 0;
	unsigned char atmos_cooldown = 0;
	bool excited = false;
	std::shared_ptr<GasMixture> air;
	Value turf_ref; // not managed because turf refcounts are very unimportant and don't matter
	std::unique_ptr<PlanetAtmosInfo> planet_atmos_info;
	std::shared_ptr<ExcitedGroup> excited_group; // shared_ptr for an actuall good reason this time.
	std::unique_ptr<MonstermosInfo> monstermos_info;
};

struct PlanetAtmosInfo
{
	ManagedValue last_initial = Value::Null();
	GasMixture last_mix = GasMixture(CELL_VOLUME);
}; // not part of main Tile struct because we don't need it for the whole map

struct MonstermosInfo
{
	int last_cycle = 0;
	uint64_t last_queue_cycle = 0;
	uint64_t last_slow_queue_cycle = 0;
	float mole_delta = 0;
	float transfer_dirs[7] = { 0,0,0,0,0,0,0 };
	float curr_transfer_amount = 0;
	float distance_score = 0;
	uint8_t curr_transfer_dir = 6;
	bool fast_done = false;
	bool is_planet = false;
};

struct ExcitedGroup : public std::enable_shared_from_this<ExcitedGroup>
{
	std::vector<Tile*> turf_list;
	int breakdown_cooldown = 0;
	int dismantle_cooldown = 0;
	void initialize();
	~ExcitedGroup();
	void reset_cooldowns();
	void merge_groups(std::shared_ptr<ExcitedGroup>& other);
	void add_turf(Tile &tile);
	void self_breakdown(bool space_is_all_consuming = false);
	void dismantle(bool unexcite = true);
};

class TurfGrid {
public:
	Tile *get(int x, int y, int z) const;
	Tile *get(int id) const;
	void refresh();

private:
	std::unique_ptr<Tile[]> tiles;
	short maxx = 0;
	short maxy = 0;
	short maxz = 0;
	int maxid = 0;
};

std::shared_ptr<GasMixture> &get_gas_mixture(Value src);
