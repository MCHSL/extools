#pragma once

#include "atmos_defines.h"

#define TOTAL_NUM_GASES 21

#define GAS_MIN_MOLES 0.00000005
#define MINIMUM_HEAT_CAPACITY	0.0003

extern float gas_specific_heat[TOTAL_NUM_GASES];

class GasMixture
{
    public:
        GasMixture(float volume);
        void mark_immutable();
        inline bool is_immutable() const {return immutable;}

        float heat_capacity() const;
        float heat_capacity_archived() const;
		void set_min_heat_capacity(float n);
        inline float total_moles() const { // inlined for debugging reasons
            float capacity = 0;
            for (int i = 0; i < TOTAL_NUM_GASES; i++) {
                capacity += moles[i];
            }
            return capacity;
        }
        float return_pressure() const;
        float thermal_energy() const;
        void archive();
        void merge(const GasMixture &giver);
        GasMixture remove(float amount);
        GasMixture remove_ratio(float ratio);
        void copy_from_mutable(const GasMixture &sample);
        float share(GasMixture &sharer, int atmos_adjacent_turfs);
        void temperature_share(GasMixture &sharer, float conduction_coefficient);
		int compare(GasMixture &sample) const;
		void clear();
		void multiply(float multiplier);

        inline float get_temperature() const { return temperature; }
        inline void set_temperature(float new_temp) { if(!immutable) temperature = new_temp; }
        inline float get_moles(int gas_type) const {return moles[gas_type] >= GAS_MIN_MOLES ? moles[gas_type] : 0;}
        inline void set_moles(int gas_type, float new_moles) { if(!immutable) moles[gas_type] = new_moles; }
		inline float get_volume() const { return volume; }
		inline void set_volume(float new_vol) { volume = new_vol; }
		inline float get_last_share() const { return last_share; }

    private:
        GasMixture();
        float moles[TOTAL_NUM_GASES];
        float moles_archived[TOTAL_NUM_GASES];
        float temperature = 0;
        float temperature_archived;
        float volume;
        float last_share = 0;
		float min_heat_capacity = 0;
        bool immutable = false;
	// you might thing, "damn, all the gases, wont that use up more memory"?
	// well no. Let's look at the average gas mixture in BYOND land containing both oxygen and nitrogen:
	// gases (28+8 bytes)
	//   oxygen (28+8 bytes)
	//     MOLES (8 bytes)
	//     MOLES_ARCHIVED (8 bytes)
	//     META_GAS_INFO (8 bytes)
	//   nitrogen (28+8 bytes)
	//     MOLES (8 bytes)
	//     MOLES_ARCHIVED (8 bytes)
	//     META_GAS_INFO (8 bytes)
	// this adds up to a total of 156 bytes. That's not even counting other vars.
};
