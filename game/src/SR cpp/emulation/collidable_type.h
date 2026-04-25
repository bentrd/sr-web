#ifndef COLLIDABLE_TYPE_H
#define COLLIDABLE_TYPE_H

#include "common.h"

namespace emu
{
	enum collidable_type : int32_t
	{
		col_invalid = -1,
		col_air = 0,
		col_square = 1,
		col_slope_ceil_left = 2,
		col_slope_ceil_right = 3,
		col_slope_floor_left = 4,
		col_slope_floor_right = 5,
		col_grapple_ceil = 6,
		col_wall_left = 7,
		col_wall_right = 8,
		col_player = 100,
		col_hook = 101,
		col_fall_tile = 102,
		col_finish_trigger = 103,
		col_saw = 104,
		col_obstacle = 105,
		col_player_start = 106,
		col_boost_section = 107,
		col_super_boost = 108,
		col_door = 109,
		col_fall_block = 110,
		col_trigger = 111,
		col_rope = 112,
		col_super_boost_wind = 113,
		col_tree = 114,
		col_dove = 115,
		col_smoke = 116,
		col_pickup = 117,
		col_checkpoint = 118,
		col_dropped_obstacle = 119,
		col_switch_block = 120,
		col_boss = 121,
		col_rocket = 122,
		col_dropped_bomb = 123,
		col_moving_platform = 124,
		col_spawn_point = 125,
		col_leaves = 126,
		col_ai_volume = 127,
		col_switch = 128,
		col_shockwave = 129,
		col_timer = 130,
		col_straight_rocket = 131,
		col_fireball = 132,
		col_bubbles = 133,
		col_bubble = 134,
		col_spark = 135,
		col_gear = 136,
		col_pump = 137,
		col_sign = 139,
		col_editable_sound_emitter = 140,
		col_boosta_coke = 141,
		col_bounce_pad = 142,
		col_ai_volume_placer = 1000,
		col_box_selector = 1001,
		col_checkpoint_placer = 1002,
		col_deco_placer = 1003
	};
}

#endif
