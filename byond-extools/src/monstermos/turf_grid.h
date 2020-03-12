#pragma once

#include "GasMixture.h"
#include "../core/core.h"
#include <memory>

class TurfGrid;
struct PlanetAtmosInfo;
struct ExcitedGroup;

extern std::vector<ExcitedGroup*> excited_groups;

struct Tile
{
    Tile();
	void update_air_ref();
	void update_adjacent(TurfGrid &grid);
	void process_cell(int fire_count);
	void archive(int fire_count);
	void last_share_check();
	// adjacent tiles in the order NORTH,SOUTH,EAST,WEST,UP,DOWN.
	// 
	Tile *adjacent[6];
	unsigned char adjacent_bits = 0;
	unsigned char atmos_cooldown = 0;
	bool excited = false;
	std::shared_ptr<GasMixture> air;
	Value turf_ref; // not managed because turf refcounts are very unimportant
	std::shared_ptr<PlanetAtmosInfo> planet_atmos_info; // shared_ptr because uhhhhhh reasons.
	std::shared_ptr<ExcitedGroup> excited_group; // shared_ptr for an actuall good reason this time.
};

struct PlanetAtmosInfo
{
	ManagedValue last_initial = Value::Null();
	GasMixture last_mix = GasMixture(CELL_VOLUME);
}; // not part of main Tile struct because we don't need it for the whole map

struct ExcitedGroup
{
	std::vector<Tile*> turf_list;
	int breakdown_cooldown = 0;
	int dismantle_cooldown = 0;
	ExcitedGroup();
	~ExcitedGroup();
	void reset_cooldowns();
	static void merge_groups(std::shared_ptr<ExcitedGroup>& us, std::shared_ptr<ExcitedGroup>& other);
	static void add_turf(std::shared_ptr<ExcitedGroup>& us, Tile &tile);
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