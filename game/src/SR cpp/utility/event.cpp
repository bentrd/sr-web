#include "event.h"
#include "../emulation/player.h"
#include "../emulation/collidable_type.h"

using namespace emu;
using namespace util;

std::string_view event::to_string(event_type event)
{
	switch (event)
	{
	case evt_none:
		return "none";
	case evt_attach_ground:
		return "attach_ground";
	case evt_attach_slope:
		return "attach_slope";
	case evt_walk_to_slope:
		return "walk_to_slope";
	case evt_walk_to_ground:
		return "walk_to_ground";
	case evt_detach_ground:
		return "detach_ground";
	case evt_detach_slope:
		return "detach_slope";
	case evt_hit_wall:
		return "hit_wall";
	case evt_hit_ceil:
		return "hit_ceil";
	case evt_attach_wall:
		return "attach_wall";
	case evt_detach_wall:
		return "detach_wall";
	case evt_hit_slope_ceil:
		return "hit_slope_ceil";
	case evt_swing_stop:
		return "swing_stop";
	}
}

static bool flat_ground(collidable_type col_id)
{
	return
		col_id == col_square || col_id == col_grapple_ceil ||
		col_id == col_wall_left || col_id == col_wall_right ||
		col_id == col_slope_ceil_right || col_id == col_slope_ceil_left;
}

static bool sloped_ground(collidable_type col_id)
{
	return
		col_id == col_slope_floor_right || col_id == col_slope_floor_left;
}

get_event_helper::get_event_helper(const player& player)
{
	m_init_time = player.m_actor->m_state->m_time;
	m_was_in_air = player.d.is_in_air;
	m_init_ground_col_id = player.d.ground_collidable_type;
	m_was_climbing = player.d.is_climbing;
	m_was_swinging = player.d.is_swinging;
	m_prev_vel = player.m_actor->d.velocity;
}

static event_type get_event_type(
	const player& player,
	const get_event_helper& helper)
{
	vector vel = player.m_actor->d.velocity;

	if (helper.m_was_in_air != player.d.is_in_air || helper.m_init_ground_col_id != player.d.ground_collidable_type)
	{
		collidable_type ground_col_id = player.d.ground_collidable_type;
		if (
			helper.m_was_in_air &&
			!player.d.is_in_air &&
			flat_ground(ground_col_id))
			return evt_attach_ground;
		else if (
			!helper.m_was_in_air &&
			!player.d.is_in_air &&
			flat_ground(helper.m_init_ground_col_id) &&
			sloped_ground(ground_col_id))
			return evt_walk_to_slope;
		else if (
			!helper.m_was_in_air &&
			!player.d.is_in_air &&
			sloped_ground(helper.m_init_ground_col_id) &&
			flat_ground(ground_col_id))
			return evt_walk_to_ground;
		else if (
			!helper.m_was_in_air &&
			player.d.is_in_air)
			return evt_detach_ground;
		else if (
			helper.m_was_in_air &&
			!player.d.is_in_air &&
			sloped_ground(ground_col_id))
			return evt_attach_slope;
	}
	if (!helper.m_was_climbing && player.d.is_climbing)
	{
		return evt_attach_wall;
	}
	if (helper.m_was_climbing && !player.d.is_climbing)
	{
		return evt_detach_wall;
	}
	if (std::abs(helper.m_prev_vel.x) > 15.0f && std::abs(vel.x) < 5.0f)
	{
		return evt_hit_wall;
	}
	if (std::abs(helper.m_prev_vel.x - vel.x) > 5.0f)
	{
		return evt_hit_slope_ceil;
	}
	if (helper.m_prev_vel.y < -15.0f && vel.y >= 0.0f /*&& player.d.is_ceiling_hit*/)
	{
		return evt_hit_ceil;
	}
	else if (helper.m_was_swinging && !player.d.is_swinging)
	{
		return evt_swing_stop;
	}
	return evt_none;
}

event get_event_helper::get_event(const player& player)
{
	event event;
	event.evt = get_event_type(player, *this);
	if (event.evt != evt_none)
	{		
		event.time = player.m_actor->m_state->m_time - m_init_time;
		event.vel_bef = m_prev_vel;
		event.vel_aft = player.m_actor->d.velocity;

		*this = get_event_helper{ player };
	}
	else
	{
		m_prev_vel = player.m_actor->d.velocity;
	}
	return event;
}
