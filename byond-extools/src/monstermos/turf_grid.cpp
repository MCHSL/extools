#include "turf_grid.h"
#include "../dmdism/opcodes.h"
#include "var_ids.h"
#include <algorithm>
#include <cstring>

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
			air = get_gas_mixture(air_ref);
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
std::vector<std::weak_ptr<ExcitedGroup>> excited_groups;

void Tile::process_cell(int fire_count) {
	if (!SSair) return;
	if (!air) {
		std::string message = (std::string("process_cell called on turf with no air! ") + std::to_string(turf_ref.value));
		Runtime((char*)message.c_str()); // ree why doesn't it accept const
		return;
	}
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
		Tile& enemy_tile = *adjacent[i];
		if (!enemy_tile.air) continue; // having no air is bad I think or something.
		if (fire_count <= enemy_tile.turf_ref.get_by_id(str_id_current_cycle)) continue;
		enemy_tile.archive(fire_count);

		bool should_share_air = false;

		if (excited_group && enemy_tile.excited_group) {
			if (excited_group != enemy_tile.excited_group) {
				excited_group->merge_groups(enemy_tile.excited_group);
			}
			should_share_air = true;
		}
		else if (air->compare(*enemy_tile.air) != -2) {
			if (!enemy_tile.excited) {
				SSair.invoke("add_to_active", { enemy_tile.turf_ref });
			}
			std::shared_ptr<ExcitedGroup> eg = excited_group;
			if (!eg)
				eg = enemy_tile.excited_group;
			if (!eg) {
				eg = std::make_shared<ExcitedGroup>();
				eg->initialize();
			}
			if (!excited_group)
				eg->add_turf(*this);
			if (!enemy_tile.excited_group)
				eg->add_turf(enemy_tile);
			should_share_air = true;
		}

		if (should_share_air) {
			// if youre like not yogs and youre porting this shit and you hate monstermos and you want spacewind just uncomment this shizz
			/*float difference = */air->share(*enemy_tile.air, adjacent_turfs_length);
			/*if (difference > 0) {
				turf_ref.invoke_by_id(str_id_consider_pressure_difference, { enemy_tile.turf_ref, difference });
			}
			else {
				enemy_tile.turf_ref.invoke_by_id(str_id_consider_pressure_difference, { turf_ref, -difference });
			}*/
			last_share_check();
		}

		if (has_planetary_atmos) {
			update_planet_atmos();
			if (air->compare(planet_atmos_info->last_mix)) {
				if (!excited_group) {
					std::shared_ptr<ExcitedGroup> eg = std::make_shared<ExcitedGroup>();
					eg->initialize();
					eg->add_turf(*this);
				}
				air->share(planet_atmos_info->last_mix, adjacent_turfs_length);
				last_share_check();
			}
		}
		turf_ref.get_by_id(str_id_air).invoke_by_id(str_id_react, { turf_ref });
		turf_ref.invoke_by_id(str_id_update_visuals, {});
		if ((!excited_group && !(air->get_temperature() > MINIMUM_TEMPERATURE_START_SUPERCONDUCTION && turf_ref.invoke("consider_superconductivity", {Value::True()})))
			|| (atmos_cooldown > (EXCITED_GROUP_DISMANTLE_CYCLES * 2))) {
			SSair.invoke("remove_from_active", { turf_ref });
		}
	}
}

void Tile::update_planet_atmos() {

	if (!planet_atmos_info || planet_atmos_info->last_initial != turf_ref.get_by_id(str_id_initial_gas_mix)) {
		Value air_ref = turf_ref.get_by_id(str_id_air);
		if (air_ref.type != DATUM || get_gas_mixture(air_ref) != air) {
			air = get_gas_mixture(air_ref);
			std::string message = (std::string("Air reference in extools doesn't match actual air, or the air is null! Turf ref: ") + std::to_string(turf_ref.value));
			Runtime((char *)message.c_str()); // ree why doesn't it accept const
			return;
		}
		if (!planet_atmos_info) planet_atmos_info = std::make_unique<PlanetAtmosInfo>();
		planet_atmos_info->last_initial = turf_ref.get_by_id(str_id_initial_gas_mix);
		GasMixture air_backup = *air;
		*air = GasMixture(CELL_VOLUME);
		turf_ref.get_by_id(str_id_air).invoke("copy_from_turf", { turf_ref });
		planet_atmos_info->last_mix = *air;
		planet_atmos_info->last_mix.archive();
		planet_atmos_info->last_mix.mark_immutable();
		*air = air_backup;
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

bool cmp_monstermos_pushorder(Tile* a, Tile* b) {
	float a_mole_delta = a->monstermos_info->mole_delta;
	float b_mole_delta = b->monstermos_info->mole_delta;
	if (a_mole_delta != b_mole_delta) {
		return a_mole_delta > b_mole_delta;
	}
	if (a_mole_delta > 0) {
		return a->monstermos_info->distance_score > b->monstermos_info->distance_score;
	} else {
		return a->monstermos_info->distance_score < b->monstermos_info->distance_score;
	}
}

uint64_t eq_queue_cycle_ctr = 0;
const int MONSTERMOS_TURF_LIMIT = 200;
const int MONSTERMOS_HARD_TURF_LIMIT = 2000;
const int opp_dir_index[] = {1, 0, 3, 2, 5, 4, 6};

void Tile::adjust_eq_movement(int dir_index, float amount) {
	monstermos_info->transfer_dirs[dir_index] += amount;
	if (dir_index != 6) {
		adjacent[dir_index]->monstermos_info->transfer_dirs[opp_dir_index[dir_index]] -= amount;
	}
}

void Tile::finalize_eq() {
	float transfer_dirs[7];
	memcpy(transfer_dirs, monstermos_info->transfer_dirs, sizeof(transfer_dirs));
	bool has_transfer_dirs = false;
	for (int i = 0; i < 7; i++) {
		if (transfer_dirs[i] != 0) {
			has_transfer_dirs = true;
			break;
		}
	}
	memset(monstermos_info->transfer_dirs, 0, sizeof(monstermos_info->transfer_dirs)); // null it out to prevent infinite recursion.
	float planet_transfer_amount = transfer_dirs[6];
	if (planet_transfer_amount > 0) {
		if (air->total_moles() < planet_transfer_amount) {
			finalize_eq_neighbors(transfer_dirs);
		}
		air->remove(planet_transfer_amount);
	} else if (planet_transfer_amount < 0) {
		update_planet_atmos();
		GasMixture planet_air = planet_atmos_info->last_mix;
		float planet_sum = planet_air.total_moles();
		if (planet_sum > 0) { // oi you can't just suck gases from turfs with no air.
			planet_air.multiply(-planet_transfer_amount / planet_sum);
			air->merge(planet_air);
		}
	}
	for (int i = 0; i < 6; i++) {
		if (!(adjacent_bits & (1 << i))) continue;
		float amount = transfer_dirs[i];
		Tile *tile = adjacent[i];
		if (amount > 0) {
			// gas push time
			// but first gotta make sure we got enough gas for that.
			if (air->total_moles() < amount) {
				finalize_eq_neighbors(transfer_dirs);
			}
			tile->monstermos_info->transfer_dirs[opp_dir_index[i]] = 0;
			tile->air->merge(air->remove(amount)); // push them gases.
			turf_ref.invoke_by_id(str_id_update_visuals, {});
			tile->turf_ref.invoke_by_id(str_id_update_visuals, {});
			turf_ref.invoke_by_id(str_id_consider_pressure_difference, { tile->turf_ref, amount });
		}
	}
}
void Tile::finalize_eq_neighbors(float *transfer_dirs) {
	for (int i = 0; i < 6; i++) {
		float amount = transfer_dirs[i];
		if (amount < 0 && (adjacent_bits & (1 << i))) {
			adjacent[i]->finalize_eq(); // just a bit of recursion if necessary.
		}
	}
}

// This proc has a worst-case running time of about O(n^2), but this is
// is really rare. Otherwise you get more like O(n*logd(n)) (there's a sort in there), or if you get lucky its faster.
void Tile::equalize_pressure_in_zone(int cyclenum) {
	// okay I lied in the proc name it equalizes moles not pressure. Pressure is impossible.
	// wanna know why? well let's say you have two turfs. One of them is 101.375 kPA and the other is 101.375 kPa.
	// When they mix what's the pressure gonna be, before any reactions occur? If you guessed 1483.62 kPa you'd be right,
	// because one of them is 101.375 kPa of hyper-noblium at 1700K temperature, and the other is 101.375 kPa of nitrogen at 43.15K temperature,
	// and that's just the way the math works out in SS13. And there's no reactions going on - hyper-noblium stops all reactions from happening.
	// I'm pretty sure real gases don't work this way. Oh yeah this property can be used to make bombs too I guess so thats neat

	if (!air || (monstermos_info && monstermos_info->last_cycle >= cyclenum)) return; // if we're already done it then piss off.

	if (monstermos_info)
		*monstermos_info = MonstermosInfo(); // null it out.
	else
		monstermos_info = std::make_shared<MonstermosInfo>();

	float starting_moles = air->total_moles();
	bool run_monstermos = false;
	// first gotta figure out if it's even necessary
	for (int i = 0; i < 6; i++) {
		if (!(adjacent_bits & (1 << i))) continue;
		Tile *other = adjacent[i];
		if (!other->air) continue;
		float comparison_moles = other->air->total_moles();
		if (std::abs(comparison_moles - starting_moles) > MINIMUM_MOLES_DELTA_TO_MOVE) {
			run_monstermos = true;
			break;
		}
	}
	if (!run_monstermos) { // if theres no need don't bother
		monstermos_info->last_cycle = cyclenum;
		return;
	}


	if (turf_ref.get_by_id(str_id_planetary_atmos).valuef) {
		return; // nah, let's not lag the server trying to process lavaland please.
	}

	// it has been deemed necessary. Now to figure out which turfs are involved.

	uint64_t queue_cycle = ++eq_queue_cycle_ctr;
	float total_moles = 0;
	std::vector<Tile*> turfs;
	turfs.push_back(this);
	monstermos_info->last_queue_cycle = queue_cycle;
	std::vector<Tile*> planet_turfs;
	for (int i = 0; i < turfs.size(); i++) { // turfs.size() increases as the loop goes on.
		if (i > MONSTERMOS_HARD_TURF_LIMIT) break;
		Tile* exploring = turfs[i];
		exploring->monstermos_info->distance_score = 0;
		if (i < MONSTERMOS_TURF_LIMIT) {
			float turf_moles = exploring->air->total_moles();
			exploring->monstermos_info->mole_delta = turf_moles;
			if (exploring->turf_ref.get_by_id(str_id_planetary_atmos).valuef) {
				planet_turfs.push_back(exploring);
				exploring->monstermos_info->is_planet = true;
				continue;
			}
			total_moles += turf_moles;
		}
		for (int j = 0; j < 6; j++) {
			if (!(exploring->adjacent_bits & (1 << j))) continue;
			Tile* adj = exploring->adjacent[j];
			if (!adj->air) continue; // no air means you're VERY UNIMPORTANT and REE STOP CAUSING SEGFAULTS
			if (adj->monstermos_info) {
				if (adj->monstermos_info->last_queue_cycle == queue_cycle) continue;
				*adj->monstermos_info = MonstermosInfo();
			} else {
				adj->monstermos_info = std::make_shared<MonstermosInfo>();
			}
			adj->monstermos_info->last_queue_cycle = queue_cycle;
			turfs.push_back(adj);
			if (adj->air->is_immutable()) {
				// Uh oh! looks like someone opened an airlock to space! TIME TO SUCK ALL THE AIR OUT!!!
				// NOT ONE OF YOU IS GONNA SURVIVE THIS
				// (I just made explosions less laggy, you're welcome)
				explosively_depressurize(cyclenum);
				return;
			}
		}
	}
	if (turfs.size() > MONSTERMOS_TURF_LIMIT) {
		for (int i = MONSTERMOS_TURF_LIMIT; i < turfs.size(); i++) {
			turfs[i]->monstermos_info->last_queue_cycle = 0; // unmark them because we shouldn't be pushing/pulling gases to/from them
		}
		turfs.resize(MONSTERMOS_TURF_LIMIT);
	}
	float average_moles = total_moles / (turfs.size() - planet_turfs.size());
	std::vector<Tile*> giver_turfs;
	std::vector<Tile*> taker_turfs;
	for (int i = 0; i < turfs.size(); i++) {
		Tile* tile = turfs[i];
		tile->monstermos_info->last_cycle = cyclenum;
		tile->monstermos_info->mole_delta -= average_moles;
		if (tile->monstermos_info->is_planet) continue;
		if (tile->monstermos_info->mole_delta > 0) {
			giver_turfs.push_back(tile);
		}
		else {
			taker_turfs.push_back(tile);
		}
	}

	float log_n = std::log2(turfs.size());
	if (giver_turfs.size() > log_n && taker_turfs.size() > log_n) { // optimization - try to spread gases using an O(nlogn) algorithm that has a chance of not working first to avoid O(n^2)
		// even if it fails, it will speed up the next part
		std::sort(turfs.begin(), turfs.end(), cmp_monstermos_pushorder);
		for (int i = 0; i < turfs.size(); i++) {
			Tile *tile = turfs[i];
			tile->monstermos_info->fast_done = true;
			if (tile->monstermos_info->mole_delta > 0) {
				uint8_t eligible_adj_bits = 0;
				int amt_eligible_adj = 0;
				for (int j = 0; j < 6; j++) {
					if (!(tile->adjacent_bits & (1 << j))) continue;
					Tile *tile2 = tile->adjacent[j];
					// skip anything that isn't part of our current processing block. Original one didn't do this unfortunately, which probably cause some massive lag.
					if (!tile2->monstermos_info || tile2->monstermos_info->fast_done || tile2->monstermos_info->last_queue_cycle != queue_cycle) continue;
					eligible_adj_bits |= (1 << j);
					amt_eligible_adj++;
				}
				if (amt_eligible_adj <= 0) continue; // Oof we've painted ourselves into a corner. Bad luck. Next part will handle this.
				float moles_to_move = tile->monstermos_info->mole_delta / amt_eligible_adj;
				for (int j = 0; j < 6; j++) {
					if (!(eligible_adj_bits & (1 << j))) continue;	
					tile->adjust_eq_movement(j, moles_to_move);
					tile->monstermos_info->mole_delta -= moles_to_move;
					tile->adjacent[j]->monstermos_info->mole_delta += moles_to_move;
				}
			}
		}
		giver_turfs.clear(); // we need to recalculate those now
		taker_turfs.clear();
		for (int i = 0; i < turfs.size(); i++) {
			Tile* tile = turfs[i];
			if (tile->monstermos_info->is_planet) continue;
			if (tile->monstermos_info->mole_delta > 0) {
				giver_turfs.push_back(tile);
			}
			else {
				taker_turfs.push_back(tile);
			}
		}
	}

	// alright this is the part that can become O(n^2).
	if (giver_turfs.size() < taker_turfs.size()) { // as an optimization, we choose one of two methods based on which list is smaller. We really want to avoid O(n^2) if we can.
		std::vector<Tile*> queue;
		queue.reserve(taker_turfs.size());
		for (int k = 0; k < giver_turfs.size(); k++) {
			Tile *giver = giver_turfs[k];
			giver->monstermos_info->curr_transfer_dir = 6;
			giver->monstermos_info->curr_transfer_amount = 0;
			uint64_t queue_cycle_slow = ++eq_queue_cycle_ctr;
			queue.clear();
			queue.push_back(giver);
			giver->monstermos_info->last_slow_queue_cycle = queue_cycle_slow;
			for (int i = 0; i < queue.size(); i++) {
				if (giver->monstermos_info->mole_delta <= 0) {
					break; // we're done here now. Let's not do more work than we need.
				}
				Tile *tile = queue[i];
				for (int j = 0; j < 6; j++) {
					if (!(tile->adjacent_bits & (1 << j))) continue;
					Tile *tile2 = tile->adjacent[j];
					if (giver->monstermos_info->mole_delta <= 0) {
						break; // we're done here now. Let's not do more work than we need.
					}
					if (!tile2->monstermos_info || tile2->monstermos_info->last_queue_cycle != queue_cycle) continue;
					if (tile2->monstermos_info->is_planet) continue;
					if (tile2->monstermos_info->last_slow_queue_cycle == queue_cycle_slow) continue;
					queue.push_back(tile2);
					tile2->monstermos_info->last_slow_queue_cycle = queue_cycle_slow;
					tile2->monstermos_info->curr_transfer_dir = opp_dir_index[j];
					tile2->monstermos_info->curr_transfer_amount = 0;
					if (tile2->monstermos_info->mole_delta < 0) {
						// this turf needs gas. Let's give it to 'em.
						if (-tile2->monstermos_info->mole_delta > giver->monstermos_info->mole_delta) {
							// we don't have enough gas
							tile2->monstermos_info->curr_transfer_amount -= giver->monstermos_info->mole_delta;
							tile2->monstermos_info->mole_delta += giver->monstermos_info->mole_delta;
							giver->monstermos_info->mole_delta = 0;
						} else {
							// we have enough gas.
							tile2->monstermos_info->curr_transfer_amount += tile2->monstermos_info->mole_delta;
							giver->monstermos_info->mole_delta += tile2->monstermos_info->mole_delta;
							tile2->monstermos_info->mole_delta = 0;
						}
					}
				}
			}
			for (int i = queue.size() - 1; i >= 0; i--) { // putting this loop here helps make it O(n^2) over O(n^3) I nearly put this in the previous loop that would have been really really slow and bad.
				Tile* tile = queue[i];
				if (tile->monstermos_info->curr_transfer_amount && tile->monstermos_info->curr_transfer_dir != 6) {
					tile->adjust_eq_movement(tile->monstermos_info->curr_transfer_dir, tile->monstermos_info->curr_transfer_amount);
					tile->adjacent[tile->monstermos_info->curr_transfer_dir]->monstermos_info->curr_transfer_amount += tile->monstermos_info->curr_transfer_amount;
					tile->monstermos_info->curr_transfer_amount = 0;
				}
			}
		}
	}
	else {
		std::vector<Tile*> queue;
		queue.reserve(giver_turfs.size());
		for (int k = 0; k < taker_turfs.size(); k++) {
			Tile *taker = taker_turfs[k];
			taker->monstermos_info->curr_transfer_dir = 6;
			taker->monstermos_info->curr_transfer_amount = 0;
			uint64_t queue_cycle_slow = ++eq_queue_cycle_ctr;
			queue.clear();
			queue.push_back(taker);
			taker->monstermos_info->last_slow_queue_cycle = queue_cycle_slow;
			for (int i = 0; i < queue.size(); i++) {
				if (taker->monstermos_info->mole_delta >= 0) {
					break; // we're done here now. Let's not do more work than we need.
				}
				Tile *tile = queue[i];
				for (int j = 0; j < 6; j++) {
					if (!(tile->adjacent_bits & (1 << j))) continue;
					Tile *tile2 = tile->adjacent[j];
					if (taker->monstermos_info->mole_delta >= 0) {
						break; // we're done here now. Let's not do more work than we need.
					}
					if (!tile2->monstermos_info || tile2->monstermos_info->last_queue_cycle != queue_cycle) continue;
					if (tile2->monstermos_info->is_planet) continue;
					if (tile2->monstermos_info->last_slow_queue_cycle == queue_cycle_slow) continue;
					queue.push_back(tile2);
					tile2->monstermos_info->last_slow_queue_cycle = queue_cycle_slow;
					tile2->monstermos_info->curr_transfer_dir = opp_dir_index[j];
					tile2->monstermos_info->curr_transfer_amount = 0;
					if (tile2->monstermos_info->mole_delta > 0) {
						// this turf has gas we can succ. Time to succ.
						if (tile2->monstermos_info->mole_delta > -taker->monstermos_info->mole_delta) {
							// they have enough gase
							tile2->monstermos_info->curr_transfer_amount -= taker->monstermos_info->mole_delta;
							tile2->monstermos_info->mole_delta += taker->monstermos_info->mole_delta;
							taker->monstermos_info->mole_delta = 0;
						}
						else {
							// they don't have neough gas
							tile2->monstermos_info->curr_transfer_amount += tile2->monstermos_info->mole_delta;
							taker->monstermos_info->mole_delta += tile2->monstermos_info->mole_delta;
							tile2->monstermos_info->mole_delta = 0;
						}
					}
				}
			}
			for (int i = queue.size() - 1; i >= 0; i--) {
				Tile* tile = queue[i];
				if (tile->monstermos_info->curr_transfer_amount && tile->monstermos_info->curr_transfer_dir != 6) {
					tile->adjust_eq_movement(tile->monstermos_info->curr_transfer_dir, tile->monstermos_info->curr_transfer_amount);
					tile->adjacent[tile->monstermos_info->curr_transfer_dir]->monstermos_info->curr_transfer_amount += tile->monstermos_info->curr_transfer_amount;
					tile->monstermos_info->curr_transfer_amount = 0;
				}
			}
		}
	}

	if (planet_turfs.size()) { // now handle planet turfs
		Tile *sample = planet_turfs[0];
		sample->update_planet_atmos();
		float planet_sum = sample->planet_atmos_info->last_mix.total_moles();
		float target_delta = planet_sum - average_moles;

		uint64_t queue_cycle_slow = ++eq_queue_cycle_ctr;
		std::vector<Tile*> progression_order;
		for (int i = 0; i < planet_turfs.size(); i++) {
			Tile *turf = planet_turfs[i];
			progression_order.push_back(turf);
			turf->monstermos_info->curr_transfer_dir = 6;
			turf->monstermos_info->last_slow_queue_cycle = queue_cycle_slow;
		}
		// now build a map of where the path to a planet turf is for each tile.
		for (int i = 0; i < progression_order.size(); i++) {
			Tile *tile = progression_order[i];
			for (int j = 0; j < 6; j++) {
				if (!(tile->adjacent_bits & (1 << j))) continue;
				Tile *tile2 = tile->adjacent[j];
				if (!tile2->monstermos_info || tile2->monstermos_info->last_queue_cycle != queue_cycle) continue;
				if (tile2->monstermos_info->last_slow_queue_cycle == queue_cycle_slow) continue;
				if (tile2->monstermos_info->is_planet) continue;
				tile->turf_ref.invoke("consider_firelocks", { tile2->turf_ref });
				if (tile->adjacent_bits & (1 << j)) {
					tile2->monstermos_info->last_slow_queue_cycle = queue_cycle_slow;
					tile2->monstermos_info->curr_transfer_dir = opp_dir_index[j];
					progression_order.push_back(tile2);
				}
			}
		}
		// apply airflow to turfs.
		for (int i = progression_order.size() - 1; i >= 0; i--) {
			Tile *tile = progression_order[i];
			float airflow = tile->monstermos_info->mole_delta - target_delta;
			tile->adjust_eq_movement(tile->monstermos_info->curr_transfer_dir, airflow);
			if (tile->monstermos_info->curr_transfer_dir != 6) {
				Tile *tile2 = tile->adjacent[tile->monstermos_info->curr_transfer_dir];
				tile2->monstermos_info->mole_delta += airflow;
			}
			tile->monstermos_info->mole_delta = target_delta;
		}
	}
	for (int i = 0; i < turfs.size(); i++) {
		turfs[i]->finalize_eq();
	}
	for (int i = 0; i < turfs.size(); i++) {
		Tile *tile = turfs[i];
		for (int j = 0; j < 6; j++) {
			if (!(tile->adjacent_bits & (1 << j))) continue;
			Tile *tile2 = tile->adjacent[j];
			if (!tile2->air) continue;
			if (tile2->air->compare(*air) != -2) {
				SSair.invoke("add_to_active", { tile->turf_ref });
				break;
			}
		}
	}
}

void Tile::explosively_depressurize(int cyclenum) {
	if (!air) return; // air is very important I think
	float total_gases_deleted = 0;
	uint64_t queue_cycle = ++eq_queue_cycle_ctr;
	std::vector<Tile*> turfs;
	std::vector<Tile*> space_turfs;
	turfs.push_back(this);
	if (monstermos_info)
		*monstermos_info = MonstermosInfo(); // null it out.
	else
		monstermos_info = std::make_shared<MonstermosInfo>();
	monstermos_info->last_queue_cycle = queue_cycle;
	monstermos_info->curr_transfer_dir = 6;
	bool warned_about_planet_atmos = false;
	for (int i = 0; i < turfs.size(); i++) {
		Tile *tile = turfs[i];
		tile->monstermos_info->last_cycle = cyclenum;
		tile->monstermos_info->curr_transfer_dir = 6;
		if (tile->turf_ref.get_by_id(str_id_planetary_atmos).valuef) {
			// planet atmos > space
			if (!warned_about_planet_atmos) {
				// warn about planet atmos
				warned_about_planet_atmos = true;
			}
			continue;
		}
		if (tile->air->is_immutable()) {
			space_turfs.push_back(tile);
			tile->turf_ref.set("pressure_specific_target", tile->turf_ref);
		} else {
			if (i > MONSTERMOS_HARD_TURF_LIMIT) continue;
			for (int j = 0; j < 6; j++) {
				if (!(tile->adjacent_bits & (1 << j))) continue;
				Tile *tile2 = tile->adjacent[j];
				if (!tile2->air) continue;
				if (tile2->monstermos_info && tile2->monstermos_info->last_queue_cycle == queue_cycle) continue;
				tile->turf_ref.invoke("consider_firelocks", {tile2->turf_ref});
				if (tile->adjacent_bits & (1 << j)) {
					if (tile2->monstermos_info)
						*tile2->monstermos_info = MonstermosInfo(); // null it out.
					else
						tile2->monstermos_info = std::make_shared<MonstermosInfo>();
					tile2->monstermos_info->last_queue_cycle = queue_cycle;
					turfs.push_back(tile2);
				}
			}
		}
	}
	if (warned_about_planet_atmos) return; // planet atmos > space

	uint64_t queue_cycle_slow = ++eq_queue_cycle_ctr;
	std::vector<Tile*> progression_order;
	for (int i = 0; i < space_turfs.size(); i++) {
		Tile *tile = space_turfs[i];
		progression_order.push_back(tile);
		tile->monstermos_info->last_slow_queue_cycle = queue_cycle_slow;
		tile->monstermos_info->curr_transfer_dir = 6;
	}
	for (int i = 0; i < progression_order.size(); i++) {
		Tile* tile = progression_order[i];
		for (int j = 0; j < 6; j++) {
			if (!(tile->adjacent_bits & (1 << j))) continue;
			Tile *tile2 = tile->adjacent[j];
			if (!tile2->monstermos_info || tile2->monstermos_info->last_queue_cycle != queue_cycle) continue;
			if (tile2->monstermos_info->last_slow_queue_cycle == queue_cycle_slow) continue;
			if (tile2->air->is_immutable()) continue;
			tile2->monstermos_info->curr_transfer_dir = opp_dir_index[j];
			tile2->monstermos_info->curr_transfer_amount = 0;
			tile2->turf_ref.set("pressure_specific_target", tile->turf_ref.get("pressure_specific_target"));
			tile2->monstermos_info->last_slow_queue_cycle = queue_cycle_slow;
			progression_order.push_back(tile2);
		}
	}
	List hpd = SSair.get("high_pressure_delta");
	for (int i = progression_order.size() - 1; i >= 0; i--) {
		Tile *tile = progression_order[i];
		if (tile->monstermos_info->curr_transfer_dir == 6) {
			continue;
		}
		int hpd_length = hpd.list->length;
		bool in_hpd = false;
		for (int i = 0; i < hpd_length; i++) {
			if (hpd.at(i) == tile->turf_ref) {
				in_hpd = true;
				break;
			}
		}
		if (!in_hpd) {
			hpd.append(tile->turf_ref);
		}
		Tile *tile2 = tile->adjacent[tile->monstermos_info->curr_transfer_dir];
		if (!tile2->air) continue;
		float sum = tile2->air->total_moles();
		total_gases_deleted += sum;
		tile->monstermos_info->curr_transfer_amount += sum;
		tile2->monstermos_info->curr_transfer_amount += tile->monstermos_info->curr_transfer_amount;
		tile->turf_ref.set("pressure_difference", tile->monstermos_info->curr_transfer_amount);
		tile->turf_ref.set("pressure_direction", 1 << tile->monstermos_info->curr_transfer_dir);
		if (tile2->monstermos_info->curr_transfer_dir == 6) {
			tile2->turf_ref.set("pressure_difference", tile2->monstermos_info->curr_transfer_amount);
			tile2->turf_ref.set("pressure_direction", 1 << tile->monstermos_info->curr_transfer_dir);
		}
		tile->air->clear();
		tile->turf_ref.invoke_by_id(str_id_update_visuals, {});
		tile->turf_ref.invoke_by_id(str_id_floor_rip, { Value(sum) });
	}
	if ((total_gases_deleted / turfs.size()) > 20 && turfs.size() > 10) { // logging I guess

	}
	return;
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
	std::unique_ptr<Tile[]> new_tiles(new Tile[new_maxx*new_maxy*new_maxz]);

	// make the thingy have actual like values or some shit I guess
	for (int z = 1; z <= new_maxz; z++) {
		for (int y = 1; y <= new_maxy; y++) {
			for (int x = 1; x <= new_maxx; x++) {
				int index = (x - 1) + new_maxx * (y - 1 + new_maxy * (z - 1));
				Tile &tile = new_tiles[index];
				if (x <= maxx && y <= maxy && z <= maxz) {
					tile = *get(x,y,z);
					tile.excited_group.reset(); // excited group contains hanging pointers now (well they're not hanging yet, but they *will* be!)
					tile.monstermos_info.reset(); // this also has hanging pointers.
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

void ExcitedGroup::initialize() {
	excited_groups.push_back(weak_from_this());
}
ExcitedGroup::~ExcitedGroup() {
	for (int i = excited_groups.size() - 1; i >= 0; i--) { // search from the end because its more likely to be at the end.
		
		if (excited_groups[i].expired()) { // get rid of the first expired one it sees because that's what this is. I'm 99% sure this won't break or cause a memory leak.
			excited_groups.erase(excited_groups.begin() + i, excited_groups.begin() + i + 1);
			break;
		}
	}
}
void ExcitedGroup::reset_cooldowns() {
	breakdown_cooldown = 0;
	dismantle_cooldown = 0;
}
void ExcitedGroup::merge_groups(std::shared_ptr<ExcitedGroup> &other) {
	std::shared_ptr<ExcitedGroup> dont_gc_yet_you_fucker = shared_from_this(); // prevent GCing before ready. Watch the compiler "optimize" this out and break shit.
	int us_size = turf_list.size();
	int other_size = other->turf_list.size();
	if (us_size > other_size) {
		for (int i = 0; i < other_size; i++) {
			Tile *tile = other->turf_list[i];
			tile->excited_group = shared_from_this();
			turf_list.push_back(tile);
		}
		other->turf_list.clear();
		reset_cooldowns();
	}
	else {
		for (int i = 0; i < us_size; i++) {
			Tile* tile = turf_list[i];
			tile->excited_group = other;
			other->turf_list.push_back(tile);
		}
		turf_list.clear();
		other->reset_cooldowns();
	}
	dont_gc_yet_you_fucker.reset(); // prevent the compiler from "optimizing" this away.
}
void ExcitedGroup::add_turf(Tile &tile) {
	turf_list.push_back(&tile);
	tile.excited_group = shared_from_this();
	reset_cooldowns();
}
void ExcitedGroup::self_breakdown(bool space_is_all_consuming) {
	GasMixture combined(CELL_VOLUME);

	int turf_list_size = turf_list.size();
	if(turf_list_size <= 0) return;
	for (int i = 0; i < turf_list_size; i++) {
		Tile& tile = *turf_list[i];
		combined.merge(*tile.air);
		if (space_is_all_consuming && tile.air->is_immutable()) {
			combined.clear();
			break;
		}
	}
	combined.multiply(1 / (float)turf_list_size);
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
