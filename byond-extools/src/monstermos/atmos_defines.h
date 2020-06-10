#pragma once

namespace monstermos::constants
{

//ATMOS
//stuff you should probably leave well alone!
const float R_IDEAL_GAS_EQUATION = 8.31;	//kPa*L/(K*mol)
const float ONE_ATMOSPHERE       = 101.325;	//kPa
const float TCMB                 = 2.7;		// -270.3degC
const float TCRYO                = 225;		// -48.15degC
const float T0C	                 = 273.15;	// 0degC
const float T20C                 = 293.15;	// 20degC

const float CELL_VOLUME				= 2500.0;	//liters in a cell
const float MOLES_CELLSTANDARD		= (ONE_ATMOSPHERE*CELL_VOLUME/(T20C*R_IDEAL_GAS_EQUATION));	//moles in a 2.5 m^3 cell at 101.325 Pa and 20 degC
const float M_CELL_WITH_RATIO		= (MOLES_CELLSTANDARD * 0.005); //compared against for superconductivity
const float O2STANDARD				= 0.21;	//percentage of oxygen in a normal mixture of air
const float N2STANDARD				= 0.79;	//same but for nitrogen
const float MOLES_O2STANDARD		= (MOLES_CELLSTANDARD*O2STANDARD);	// O2 standard value (21%)
const float MOLES_N2STANDARD		= (MOLES_CELLSTANDARD*N2STANDARD);	// N2 standard value (79%)
const float BREATH_VOLUME			= 0.5;		//liters in a normal breath
const float BREATH_PERCENTAGE		= (BREATH_VOLUME/CELL_VOLUME);					//Amount of air to take a from a tile

//EXCITED GROUPS
const int EXCITED_GROUP_BREAKDOWN_CYCLES				= 4;		//number of FULL air controller ticks before an excited group breaks down (averages gas contents across turfs)
const int EXCITED_GROUP_DISMANTLE_CYCLES				= 16;		//number of FULL air controller ticks before an excited group dismantles and removes its turfs from active

const float MINIMUM_AIR_RATIO_TO_SUSPEND				= 0.1;		//Ratio of air that must move to/from a tile to reset group processing
const float MINIMUM_AIR_RATIO_TO_MOVE					= 0.001;	//Minimum ratio of air that must move to/from a tile
const float MINIMUM_AIR_TO_SUSPEND						= (MOLES_CELLSTANDARD*MINIMUM_AIR_RATIO_TO_SUSPEND);	//Minimum amount of air that has to move before a group processing can be suspended
const float MINIMUM_MOLES_DELTA_TO_MOVE					= (MOLES_CELLSTANDARD*MINIMUM_AIR_RATIO_TO_MOVE); //Either this must be active
const float MINIMUM_TEMPERATURE_TO_MOVE					= (T20C+100.0);			//or this (or both, obviously)
const float MINIMUM_TEMPERATURE_DELTA_TO_SUSPEND		= 4.0;		//Minimum temperature difference before group processing is suspended
const float MINIMUM_TEMPERATURE_DELTA_TO_CONSIDER		= 0.5;		//Minimum temperature difference before the gas temperatures are just set to be equal
const float MINIMUM_TEMPERATURE_FOR_SUPERCONDUCTION		= (T20C+10.0);
const float MINIMUM_TEMPERATURE_START_SUPERCONDUCTION	= (T20C+200.0);

//HEAT TRANSFER COEFFICIENTS
//Must be between 0 and 1. Values closer to 1 equalize temperature faster
//Should not exceed 0.4 else strange heat flow occur
const float WALL_HEAT_TRANSFER_COEFFICIENT		= 0.0;
const float OPEN_HEAT_TRANSFER_COEFFICIENT		= 0.4;
const float WINDOW_HEAT_TRANSFER_COEFFICIENT	= 0.1;		//a hack for now
const float HEAT_CAPACITY_VACUUM				= 7000.0;	//a hack to help make vacuums "cold", sacrificing realism for gameplay

//FIRE
const float FIRE_MINIMUM_TEMPERATURE_TO_SPREAD	= (150.0+T0C);
const float FIRE_MINIMUM_TEMPERATURE_TO_EXIST	= (100.0+T0C);
const float FIRE_SPREAD_RADIOSITY_SCALE			= 0.85;
const float FIRE_GROWTH_RATE					= 40000;	//For small fires
const float PLASMA_MINIMUM_BURN_TEMPERATURE		= (100.0+T0C);
const float PLASMA_UPPER_TEMPERATURE			= (1370.0+T0C);
const float PLASMA_OXYGEN_FULLBURN				= 10;

//GASES
const int MIN_TOXIC_GAS_DAMAGE				= 1;
const int MAX_TOXIC_GAS_DAMAGE				= 10;
const float MOLES_GAS_VISIBLE				= 0.25;	//Moles in a standard cell after which gases are visible

const float FACTOR_GAS_VISIBLE_MAX				= 20; //moles_visible * FACTOR_GAS_VISIBLE_MAX = Moles after which gas is at maximum visibility
const float MOLES_GAS_VISIBLE_STEP				= 0.25; //Mole step for alpha updates. This means alpha can update at 0.25, 0.5, 0.75 and so on

//REACTIONS
//return values for reactions (bitflags)
const int NO_REACTION       = 0;
const int REACTING          = 1;
const int STOP_REACTIONS    = 2;

// Pressure limits.
const float HAZARD_HIGH_PRESSURE				= 550;		//This determins at what pressure the ultra-high pressure red icon is displayed. (This one is set as a constant)
const float WARNING_HIGH_PRESSURE				= 325;		//This determins when the orange pressure icon is displayed (it is 0.7 * HAZARD_HIGH_PRESSURE)
const float WARNING_LOW_PRESSURE				= 50;		//This is when the gray low pressure icon is displayed. (it is 2.5 * HAZARD_LOW_PRESSURE)
const float HAZARD_LOW_PRESSURE					= 20;		//This is when the black ultra-low pressure icon is displayed. (This one is set as a constant)

const float TEMPERATURE_DAMAGE_COEFFICIENT		= 1.5;		//This is used in handle_temperature_damage() for humans, and in reagents that affect body temperature. Temperature damage is multiplied by this amount.

const float BODYTEMP_NORMAL						= 310.15;			//The natural temperature for a body
const float BODYTEMP_AUTORECOVERY_DIVISOR		= 11;		//This is the divisor which handles how much of the temperature difference between the current body temperature and 310.15K (optimal temperature) humans auto-regenerate each tick. The higher the number, the slower the recovery. This is applied each tick, so long as the mob is alive.
const float BODYTEMP_AUTORECOVERY_MINIMUM		= 12;		//Minimum amount of kelvin moved toward 310K per tick. So long as abs(310.15 - bodytemp) is more than 50.
const float BODYTEMP_COLD_DIVISOR				= 6;		//Similar to the BODYTEMP_AUTORECOVERY_DIVISOR, but this is the divisor which is applied at the stage that follows autorecovery. This is the divisor which comes into play when the human's loc temperature is lower than their body temperature. Make it lower to lose bodytemp faster.
const float BODYTEMP_HEAT_DIVISOR				= 15;		//Similar to the BODYTEMP_AUTORECOVERY_DIVISOR, but this is the divisor which is applied at the stage that follows autorecovery. This is the divisor which comes into play when the human's loc temperature is higher than their body temperature. Make it lower to gain bodytemp faster.
const float BODYTEMP_COOLING_MAX				= -100;		//The maximum number of degrees that your body can cool in 1 tick, due to the environment, when in a cold area.
const float BODYTEMP_HEATING_MAX				= 30;		//The maximum number of degrees that your body can heat up in 1 tick, due to the environment, when in a hot area.

const float BODYTEMP_HEAT_DAMAGE_LIMIT			= (BODYTEMP_NORMAL + 50); // The limit the human body can take before it starts taking damage from heat.
const float BODYTEMP_COLD_DAMAGE_LIMIT			= (BODYTEMP_NORMAL - 50); // The limit the human body can take before it starts taking damage from coldness.


const float SPACE_HELM_MIN_TEMP_PROTECT			= 2.0;		//what min_cold_protection_temperature is set to for space-helmet quality headwear. MUST NOT BE 0.
const float SPACE_HELM_MAX_TEMP_PROTECT			= 1500;	//Thermal insulation works both ways /Malkevin
const float SPACE_SUIT_MIN_TEMP_PROTECT			= 2.0;		//what min_cold_protection_temperature is set to for space-suit quality jumpsuits or suits. MUST NOT BE 0.
const float SPACE_SUIT_MAX_TEMP_PROTECT			= 1500;

const float FIRE_SUIT_MIN_TEMP_PROTECT			= 60;		//Cold protection for firesuits
const float FIRE_SUIT_MAX_TEMP_PROTECT			= 30000;	//what max_heat_protection_temperature is set to for firesuit quality suits. MUST NOT BE 0.
const float FIRE_HELM_MIN_TEMP_PROTECT			= 60;		//Cold protection for fire helmets
const float FIRE_HELM_MAX_TEMP_PROTECT			= 30000;	//for fire helmet quality items (red and white hardhats)

const float FIRE_IMMUNITY_MAX_TEMP_PROTECT	 = 35000;		//what max_heat_protection_temperature is set to for firesuit quality suits and helmets. MUST NOT BE 0.

const float HELMET_MIN_TEMP_PROTECT				= 160;		//For normal helmets
const float HELMET_MAX_TEMP_PROTECT				= 600;		//For normal helmets
const float ARMOR_MIN_TEMP_PROTECT				= 160;		//For armor
const float ARMOR_MAX_TEMP_PROTECT				= 600;		//For armor

const float GLOVES_MIN_TEMP_PROTECT				= 2.0;		//For some gloves (black and)
const float GLOVES_MAX_TEMP_PROTECT				= 1500;	//For some gloves
const float SHOES_MIN_TEMP_PROTECT				= 2.0;		//For gloves
const float SHOES_MAX_TEMP_PROTECT				= 1500;	//For gloves

const float PRESSURE_DAMAGE_COEFFICIENT			= 4;		//The amount of pressure damage someone takes is equal to (pressure / HAZARD_HIGH_PRESSURE)*PRESSURE_DAMAGE_COEFFICIENT, with the maximum of MAX_PRESSURE_DAMAGE
const float MAX_HIGH_PRESSURE_DAMAGE			= 4;
const float LOW_PRESSURE_DAMAGE					= 4;		//The amount of damage someone takes when in a low pressure area (The pressure threshold is so low that it doesn't make sense to do any calculations, so it just applies this flat value).

const float COLD_SLOWDOWN_FACTOR				= 20;		//Humans are slowed by the difference between bodytemp and BODYTEMP_COLD_DAMAGE_LIMIT divided by this

//PIPES
//Atmos pipe limits
const float MAX_OUTPUT_PRESSURE					= 4500; // (kPa) What pressure pumps and powered equipment max out at.
const float MAX_TRANSFER_RATE					= 200; // (L/s) Maximum speed powered equipment can work at.
const float VOLUME_PUMP_LEAK_AMOUNT				= 0.1; //10% of an overclocked volume pump leaks into the air
//used for device_type vars
const int UNARY         = 1;
const int BINARY        = 2;
const int TRINARY       = 3;
const int QUATERNARY    = 4;

//TANKS
const float TANK_MELT_TEMPERATURE				= 1000000;	//temperature in kelvins at which a tank will start to melt
const float TANK_LEAK_PRESSURE					= (30.*ONE_ATMOSPHERE);	//Tank starts leaking
const float TANK_RUPTURE_PRESSURE				= (35.*ONE_ATMOSPHERE);	//Tank spills all contents into atmosphere
const float TANK_FRAGMENT_PRESSURE				= (40.*ONE_ATMOSPHERE);	//Boom 3x3 base explosion
const float TANK_FRAGMENT_SCALE	    			= (6.*ONE_ATMOSPHERE);		//+1 for each SCALE kPa aboe threshold
const float TANK_MAX_RELEASE_PRESSURE 			= (ONE_ATMOSPHERE*3);
const float TANK_MIN_RELEASE_PRESSURE 			= 0;
const float TANK_DEFAULT_RELEASE_PRESSURE 		= 16;

}