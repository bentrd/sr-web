#ifndef PHYSICS_CONSTANTS_H
#define PHYSICS_CONSTANTS_H

#include "vector.h"

namespace emu::physics
{
	constexpr float min_slide_speed = 300.0f;
	constexpr float half_running_speed = 300.0f;
	constexpr float running_speed = 600.0f;
	constexpr float slow_speed = 300.0f;
	constexpr float fast_speed = 750.0f;
	constexpr float boosting_speed = 900.0f;
	constexpr float super_boost_speed = 1200.0f;
	constexpr float max_speed = 1500.0f;
	constexpr float climb_up_speed = 160.0f;

	constexpr float jump_strength = 2876.0f;
	constexpr float wall_jump_strength = 1500.0f;
	constexpr float jump_push = 200.0f;
	constexpr float wall_jump_y_velocity_multiplier = 0.5f;

	constexpr vector gravity = { 0.0f, 1000.0f };
	constexpr float turnaround_base_acceleration = 1200.0f;
	constexpr float stunned_base_acceleration = 600.0f;
	constexpr float acceleration_low = 250.0f;
	constexpr float acceleration_high = 900.0f;
	constexpr float swing_deceleration = 100.0f;
	constexpr float air_acceleration = 150.0f;
	constexpr float wall_jump_bonus_acceleration = 600.0f;
	constexpr float deceleration_low = 200.0f;
	constexpr float deceleration_medium = 300.0f;
	constexpr float deceleration_high = 600.0f;
	constexpr float boost_acceleration_high = 2400.0f;
	constexpr float boost_acceleration_medium = 1200.0f;
	constexpr float boost_acceleration_low = 600.0f;
	constexpr float slide_deceleration_high = 400.0f;
	constexpr float slide_deceleration_medium = 275.0f;
	constexpr float slide_deceleration_low = 100.0f;
	constexpr float hooked_deceleration = 1200.0f;

	constexpr float stumble_duration = 0.4f;
	constexpr float grapple_cooldown = 0.2f;
	constexpr float slide_cooldown = 0.5f;
	constexpr float slide_cancel_cooldown = 0.5f;
	constexpr float jump_duration = 0.25f;
	constexpr float wall_jump_buffer_cooldown = 0.25f;
	constexpr float drill_duration = 3.0f;
	constexpr float round_start_item_cooldown = 1.5f;

	constexpr float boost_charge_rate = 3.0f;
	constexpr float boost_consumption_rate = 0.85f;

	constexpr float unused = 200.0f;

	constexpr float grapple_hook_speed = 3000.0f;
}

#endif