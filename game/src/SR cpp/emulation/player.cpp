#include <cmath>
#include <algorithm>
#include <iostream>

#include "common.h"
#include "player.h"
#include "tile_actor.h"
#include "sr_math.h"
#include "state.h"
#include "caches.h"
#include "physics_constants.h"
#include "super_boost_volume.h"
#include "obstacle.h"

using namespace emu;

std::unique_ptr<i_clonable> player::clone() const
{
	return std::make_unique<player>(*this);
}

bool player::set(const i_actor_controller* other)
{
	if (const player* ptr_cast = dynamic_cast<const player*>(other))
	{
		*this = *ptr_cast;
		return true;
	}
	return false;
}

void player::replace_pointers(const std::map<const i_actor_controller*, i_actor_controller*>& map)
{
	auto it = map.find(m_grapple);
	if (it != map.end())
		m_grapple = dynamic_cast<grapple*>(it->second);
}

void player::set_position(vector position)
{
	m_actor->set_position(position);
	update_hitboxes();
}

actor_init_params player::get_actor_params()
{
	return
	{
		.size = vector{ 25.0f, 45.0f },
		.auto_col_det = true,
		.is_col = true,
		.should_pred_col = true,
	};
}

actor* player::get_actor()
{
	return m_actor;
}

void player::set_actor(actor* actor)
{
	m_actor = actor;
}

i_collision_shape* player::get_collision()
{
	if (d.is_sliding || d.is_inside_super_boost_wind || d.is_inside_super_boost || d.bounce_pad_timer > 0.0f)
		return &m_sliding_hitbox;
	return &m_standing_hitbox;
}

collision_filter& player::get_collision_filter()
{
	return d.collision_filter1;
}

collidable_type player::get_collidable_type() const
{
	return col_player;
}

void player::init()
{
	if (d.player_game_mode != 0)
	{
		d.enable_grapples = true;
		d.enable_climbing = true;
		d.enable_double_jump = true;
	}
	d.is_destroyed = false;
	init_hitboxes();
	m_grapple = m_actor->m_state->spawn<grapple>(vec_zero);
	m_grapple->m_owner = this;
	m_player_index = (int32_t)m_actor->m_state->count<player>();
}

void player::init_hitboxes()
{
	m_standing_hitbox = aabb{
		m_actor->d.position.x, m_actor->d.position.x + 25.0f,
		m_actor->d.position.y, m_actor->d.position.y + 45.0f
	};
	m_sliding_hitbox = aabb{
		m_actor->d.position.x, m_actor->d.position.x + 25.0f,
		m_actor->d.position.y + 20.0f, m_actor->d.position.y + 45.0f
	};
	m_actor->d.collision_sweep_minimal_density = 2.0f;
}

void player::reset()
{
	m_actor->d.is_collision_active = true;
	m_actor->d.velocity = vec_zero;
	d.base_acceleration = 0.0f;
	d.displacement = vec_zero;
	// todo
	d.unknown6 = 0.0f;
	d.jump_count = 2;
	d.jump_state = 0;
	d.next_jump_state = 1;
	d.jump_velocity = vec_zero;
	d.initial_jump_velocity = vec_zero;
	d.is_on_ground = false;
	d.is_on_fall_tile = false;
	d.can_buffer_wall_jump = false;
	d.is_in_air = false;
	d.can_repress_jump = true;
	d.queued_jump = false;
	d.was_jump_held = false;
	d.is_sliding = false;
	d.is_stumbling = false;
	d.is_tackled = false;
	d.can_grapple = true;
	d.is_shooting = false;
	d.is_swinging = false;
	d.is_using_boost = false;
	d.is_inside_super_boost_wind = false;
	d.is_inside_super_boost = false;
	d.bounce_pad_timer = 0.0f;
	d.is_climbing = false;
	d.is_on_wall = false;
	d.is_hooked2 = false;
	d.has_touched_finish_bomb = false;
	d.is_using_drill = false;
	d.is_taunting = false;
	d.is_still_alive = false;
	d.move_direction = 1;
	d.collected_sandals = 0;
	d.is_sudden_death = false;
	d.is_dying2 = false;
	d.is_stunned = false;
	d.stunned_by_id = INT32_MIN;
	// todo
	d.is_dying = false;
	d.killed_by_id = 0;
	// todo
	d.is_frozen = false;
	d.frozen_timer = 0.0f;
	d.travel_distance = 0.0f;
	d.timer = 0.0f;
	d.unknown22 = false;
	d.unknown23 = 0.0f;
}

float player::get_velocity_multiplier()
{
	float num = 0.02f;
	// ignore team code
	return std::min(d.boostacoke.get(), 10.0f) * num + 1.0f;
}

float player::get_acceleration_multiplier()
{
	float num = 0.04f;
	return std::min(d.boostacoke.get(), 10.0f) * num + 1.0f;
}

void player::update_hitboxes()
{
	m_standing_hitbox.set_position(m_actor->d.position);
	m_sliding_hitbox.set_position(m_actor->d.position + vector{ 0.0f, 20.0f });
	m_actor->m_bounds = aabb{
		m_actor->d.position.x, m_actor->d.position.x + m_actor->get_size().x,
		m_actor->d.position.y, m_actor->d.position.y + m_actor->get_size().y
	};
}

void player::update_basic(timespan time, timespan delta)
{
	vector velocity = m_actor->d.velocity;
	if (d.boost_cooldown > 0)
		d.boost_cooldown -= d.delta;
	if (d.wall_jump_bonus_timer > 0)
		d.wall_jump_bonus_timer -= d.delta;
	if (d.is_dying)
	{
		velocity -= d.collision_tangent * 0.15f * physics::stunned_base_acceleration * d.delta;
		velocity += physics::gravity * d.delta;
		m_actor->d.velocity = velocity;
	}
	else if (d.is_hooked2 && true) // todo
	{
		
	}
	else
	{
		if (!d.is_on_ground && !d.is_climbing && d.ground_collidable_type == col_air && d.jump_state == 0)
		{
			d.is_in_air = true;
			d.jump_state = 1;
			d.next_jump_state = d.jump_count;
		}
		if (d.is_stunned)
		{
			if (!d.is_on_ground)
			{
				float x = velocity.x;
				if ((d.collision_tangent.x >= 0.0f && d.right_held && !d.left_held) || (d.collision_tangent.x <= 0.0f && d.left_held && !d.right_held))
					velocity -= d.collision_tangent * 0.05f * physics::stunned_base_acceleration * d.delta;
				else if ((d.collision_tangent.x < 0.0f && d.right_held && !d.left_held) || (d.collision_tangent.x > 0.0f && d.left_held && !d.right_held))
					velocity -= d.collision_tangent * 0.25f * physics::stunned_base_acceleration * d.delta;
				else if (velocity.x != 0.0f)
				{
					velocity -= d.collision_tangent * 0.15f * physics::stunned_base_acceleration * d.delta;
					if ((x > 0.0f && velocity.x < 0.0f) || (x < 0.0f && velocity.x > 0.0f))
						velocity.x = 0.0f;
				}
				velocity -= physics::gravity * d.delta;
			}
			else if (velocity.x * d.collision_tangent.x > 0.0f)
				velocity -= d.collision_tangent * physics::stunned_base_acceleration * d.delta;
			else
				velocity = vec_zero;
			m_actor->d.velocity = velocity;
			// todo
		}
		else if (d.is_frozen)
		{
			if (d.is_inside_super_boost_wind || d.is_inside_super_boost)
			{
				velocity = d.collision_tangent * physics::super_boost_speed;
				if (d.collision_tangent.x > 0.0f)
					d.move_direction = 1;
				else if (d.collision_tangent.x < 0.0f)
					d.move_direction = -1;
			}
			else if (d.bounce_pad_timer > 0.0f)
				velocity = d.super_boost_force;
			else
			{
				if (d.is_on_ground && d.collision_tangent.y > 0.0f)
					velocity += d.collision_tangent * physics::acceleration_low * d.delta;
				else if (velocity.x * d.collision_tangent.x > 0.0f)
					velocity -= d.collision_tangent * 0.35f * physics::stunned_base_acceleration * d.delta;
				else
					velocity.x = 0.0f;
				if (!d.is_on_ground)
					velocity += physics::gravity * d.delta;
			}
			d.frozen_timer -= d.delta;
			if (d.frozen_timer < 0.0f)
			{
				unfreeze();
			}
			m_actor->d.velocity = velocity;
			// todo
		}
		else
		{
			if ((d.is_stumbling || d.is_tackled) && (time - d.stumble_time).seconds() > physics::stumble_duration)
			{
				d.is_stumbling = false;
				d.is_tackled = false;
			}
			if (d.is_taunting)
			{
				d.left_held = false;
				d.right_held = false;
				d.jump_held = false;
				d.slide_held = false;
			}
			update_base_acceleration(velocity);
			float x = velocity.x;
			if (!d.is_climbing && !d.is_sliding && !m_grapple->d.is_connected && !d.is_stunned && !d.is_dying && d.right_held && !d.left_held)
			{
				if (d.collision_tangent.x >= 0.0f || d.base_acceleration < 0.0f)
					velocity += d.collision_tangent * d.base_acceleration * d.delta;
				else
					velocity -= d.collision_tangent * d.base_acceleration * d.delta;
				if (x <= 0.0f && velocity.x > 0.0f)
					d.move_direction = 1;
			}
			else if (!d.is_climbing && !d.is_sliding && !m_grapple->d.is_connected && !d.is_stunned && !d.is_dying && d.left_held && !d.right_held)
			{
				if (d.collision_tangent.x <= 0.0f || d.base_acceleration < 0.0f)
					velocity += d.collision_tangent * d.base_acceleration * d.delta;
				else
					velocity -= d.collision_tangent * d.base_acceleration * d.delta;
				if (x >= 0.0f && velocity.x < 0.0f)
					d.move_direction = -1;
			}
			else if (!m_grapple->d.is_connected && velocity.x != 0.0f)
			{
				if (d.is_on_ground)
				{
					if (d.is_sliding)
						velocity += d.collision_tangent * d.base_acceleration * d.delta;
					else
						velocity -= d.collision_tangent * 600.0f * d.delta;
				}
				else
					velocity -= d.collision_tangent * std::abs(d.base_acceleration) * d.delta;
				if ((x > 0.0f && velocity.x < 0.0f) || (x < 0.0f && velocity.x > 0.0f))
					velocity.x = 0.0f;
			}
			else if (m_grapple->d.is_connected)
			{
				if (d.is_swinging)
				{
					float speed = velocity.length();
					if (speed > 0.0f)
						velocity = velocity.normalized();
					if (speed > physics::fast_speed * get_velocity_multiplier())
						velocity *= speed - physics::swing_deceleration * d.delta;
					else if (speed > 0.0f)
						velocity *= physics::fast_speed * get_velocity_multiplier();
					else
						velocity = d.collision_tangent * physics::fast_speed * get_velocity_multiplier();
				}
				else
				{
					int direction = (m_grapple->d.direction.x < 0.0f) ? -1 : 1;
					// irrelevant code
					float radius = (m_actor->get_collision()->get_center() - m_grapple->get_center()).length(); // todo
					connect_grapple(m_grapple->get_center(), radius, direction); // todo
				}
			}
			else
				velocity.x = 0.0f;

			if (!m_grapple->d.is_connected && !d.is_grappling && ((time - d.slide_time).seconds() > physics::slide_cooldown || d.is_sliding) && d.slide_held && velocity.length() > physics::min_slide_speed && !d.was_slide_cancelled)
			{
				if (d.is_on_ground)
					// ignore sound code
					d.is_foley_slide = true;
				if (!d.is_sliding)
					d.slide_time = time;
				d.is_sliding = true;
				d.slide_cancel_time = time;
			}
			if (d.is_sliding && (!d.slide_held || d.is_grappling || d.displacement.length() < 0.0001f))
			{
				d.was_slide_stopped = true;
				d.is_sliding = false;
				d.is_foley_slide = false;
			}
			if (d.was_slide_cancelled && (!d.slide_held || !d.jump_held || (time - d.slide_cancel_time).seconds() > physics::slide_cancel_cooldown))
				d.was_slide_cancelled = false;
			// Fire either a live jump_held OR a buffered press that was
			// queued during a slide. The queued press is only consumable
			// once the slide has ended, which matches the user-visible
			// "jump fires when slide finishes" semantic.
			bool jump_trigger = d.jump_held;
			if (!d.is_sliding && d.queued_jump)
			{
				jump_trigger = true;
				d.queued_jump = false;
			}
			if (jump_trigger && !d.is_stunned && !d.is_dying && !m_grapple->d.is_connected && (!d.is_sliding || d.jump_state < 1))
			{
				// Consume the buffer if a fresh press fired through the
				// fast path — avoids a second jump on the next frame after
				// slide ends.
				d.queued_jump = false;
				if (d.is_foley_slide)
				{
					d.is_sliding = false;
					d.is_foley_slide = false;
					d.was_slide_cancelled = true;
				}
				if (d.can_repress_jump)
					d.repress_jump_time = time;
				if (d.can_repress_jump && (!d.is_in_air || d.jump_state < d.next_jump_state))
				{
					if (!d.is_in_air)
					{
						d.jump_state = 1;
						// ignore irrelevant code
						d.jump_velocity = vec_zero;
						d.initial_jump_velocity = vec_zero;
					}
					else
					{
						velocity.y = std::min(velocity.y, 0.0f);
						d.jump_velocity.y = 0.0f;
						d.jump_state++;
						if (d.item_id == 7 && d.jump_state == 3)
						{
							d.item_id = 0;
							d.jump_count = 2;
						}
						if (d.left_held && !d.right_held)
						{
							vector jump_push = vec_up_left * physics::jump_push;
							velocity.y = std::min(velocity.y, jump_push.y);
							if (velocity.x > 0.0f)
								velocity.x += jump_push.x;
						}
						else if (d.right_held && !d.left_held)
						{
							vector jump_push = vec_up_right * 200.0f;
							velocity.y = std::min(velocity.y, jump_push.y);
							if (velocity.x < 0.0f)
								velocity.x += jump_push.x;
						}
					}
					// ignore particle code
					d.is_in_air = true;
					d.jump_time = time - delta;
					// todo
					if (d.is_climbing)
					{
						if (d.move_direction == 1)
							d.jump_direction = vector{ -1.0f, -7.0f }.normalized();
						else
							d.jump_direction = vector{ 1.0f, -7.0f }.normalized();
						d.is_climbing = false;
						d.wall_get_off_time = time;
						d.jump_strength = physics::wall_jump_strength;
						d.move_direction *= -1;
						d.initial_jump_velocity.y = std::min(0.0f, physics::wall_jump_y_velocity_multiplier * velocity.y);
						d.jump_duration = physics::jump_duration * 0.65f;
						velocity.x = d.jump_direction.x * d.jump_strength;
					}
					else
					{
						d.jump_direction = vec_up;
						d.jump_duration = physics::jump_duration;
					}
					// ignore sound code
				}
				else if (d.jump_state == 2 && d.repress_jump_time.seconds() > time.seconds() - physics::wall_jump_buffer_cooldown)
					d.can_buffer_wall_jump = true;
				else
					d.can_buffer_wall_jump = false;
				if (d.is_in_air)
					update_jump(time, delta, velocity, d.jump_time);
				d.can_repress_jump = false;
			}
			else if (!d.jump_held)
			{
				d.can_repress_jump = true;
				d.can_buffer_wall_jump = false;
				d.initial_jump_velocity = vec_zero;
			}
			if (d.is_in_air && !m_grapple->d.is_connected)
			{
				if (d.left_held && !d.right_held)
				{
					if (d.wall_jump_bonus_timer > 0.0f && velocity.x < 0.0f && velocity.x > physics::running_speed * 0.8f)
						velocity.x -= physics::wall_jump_bonus_acceleration * d.delta;
					else
						velocity.x -= physics::air_acceleration * d.delta;
					if (velocity.x < 0.0f)
						d.move_direction = -1;
				}
				else if (d.right_held && !d.left_held)
				{
					if (d.wall_jump_bonus_timer > 0.0f && velocity.x > 0.0f && velocity.x < physics::running_speed * 0.8f)
						velocity.x += physics::wall_jump_bonus_acceleration * d.delta;
					else
						velocity.x += physics::air_acceleration * d.delta;
					if (velocity.x > 0.0f)
						d.move_direction = 1;
				}
				if (std::abs(velocity.x) > physics::fast_speed)
					velocity.x += -physics::deceleration_medium * (float)d.move_direction * d.delta;
				else if (std::abs(velocity.x) > physics::slow_speed)
					velocity.x -= physics::deceleration_low * (float)d.move_direction * d.delta;
			}
			if (d.jump_state == d.next_jump_state && !d.jump_held)
			{
				d.next_jump_state = std::min(d.next_jump_state + 1, (int32_t)d.jump_count);
				d.jump_strength = physics::jump_strength;
				d.jump_direction = vec_up;
				d.jump_velocity.x = 0.0f;
			}
			if (d.is_swinging)
			{
				vector grapple_center = m_grapple->get_center();
				vector center = m_actor->get_collision()->get_center();
				float act_rad = (grapple_center - (center + velocity * d.delta)).length();
				float speed = velocity.length();
				vector line = center - grapple_center;
				d.swing_angle = (float)std::atan2((double)line.y, (double)line.x);
				if (d.is_on_ground && act_rad < d.swing_radius)
					d.swing_radius = line.length();
				else if (speed > 0.0f)
				{
					float delta_angle = (float)std::asin((double)std::clamp(speed * 0.5f * d.delta / d.swing_radius, -1.0f, 1.0f)) * 2.0f;
					if (d.move_direction == 1)
						delta_angle = -delta_angle;
					float new_angle = d.swing_angle + delta_angle;
					if (new_angle < -0.3141592700403172 && new_angle > -2.827433383549476)
					{
						cancel_grapple();
					}
					else
					{
						velocity = vector{ (float)(std::cos((double)new_angle) * d.swing_radius), (float)(std::sin((double)new_angle) * d.swing_radius) } + grapple_center - center;
						velocity = velocity.normalized() * speed;
					}
				}
			}
			// todo
			if ((d.is_using_boost || d.is_using_drill) && (!d.boost_held || d.boost <= 0.0f))
				d.is_using_boost = false;
			if (d.is_climbing)
			{
				if (velocity.y < 0.0f)
					velocity.y += physics::acceleration_high * d.delta;
				else
					velocity.y += 0.5f * physics::acceleration_high * d.delta;
				velocity.x = (float)d.move_direction;
				if (d.is_on_ground)
				{
					d.is_climbing = false;
					d.wall_get_off_time = time;
				}
			}
			if (d.is_inside_super_boost || d.is_inside_super_boost_wind || d.bounce_pad_timer > 0.0f)
			{
				float num = velocity.dot(d.super_boost_direction);
				if (num > physics::super_boost_speed)
					velocity = d.collision_tangent * num;
				else
					velocity = d.super_boost_force;
				if (d.collision_tangent.x > 0.0f)
					d.move_direction = 1;
				else if (d.collision_tangent.x < 0.0f)
					d.move_direction = -1;
			}
			if (!m_actor->m_state->no_speed_cap && velocity.length() > physics::max_speed)
			{
				velocity = physics::max_speed * velocity.normalized();
			}
			if (d.unused2)
			{
				velocity.y = std::min(velocity.y, -physics::unused);
				velocity.x = 0.0f;
			}
			else if (d.is_climbing_up)
			{
				velocity.y = std::min(velocity.y, -physics::climb_up_speed);
				velocity.x = d.move_direction * std::max(std::abs(velocity.x), physics::climb_up_speed);
			}
			else if (d.unused3)
			{
				velocity.y = std::min(velocity.y, -physics::unused);
				velocity.x = d.move_direction * 0.5f * physics::unused;
			}
			if (!d.is_on_ground && !m_grapple->d.is_connected && !d.is_climbing)
				velocity += physics::gravity * d.delta;
			m_actor->d.velocity = velocity;
			// ignore sound code
			// todo
		}
	}
}

void player::update_base_acceleration(vector velocity)
{
	float speed = velocity.length();
	if (d.is_climbing)
	{
		d.base_acceleration = 0.0f;
		return;
	}
	if ((d.right_held && !d.left_held && velocity.x < 0.0f) || (d.left_held && !d.right_held && velocity.x > 0.0f))
	{
		d.base_acceleration = -physics::turnaround_base_acceleration;
		if (velocity.y < 0.0f && d.is_on_ground && !d.is_in_air)
		{
			d.base_acceleration *= 1.5f;
		}
	}
	else if (d.is_using_drill)
	{
		if (speed < physics::half_running_speed * get_velocity_multiplier())
			d.base_acceleration = physics::boost_acceleration_high;
		else if (speed < physics::running_speed * get_velocity_multiplier())
			d.base_acceleration = physics::boost_acceleration_medium;
		else if (speed < physics::boosting_speed * get_velocity_multiplier())
			d.base_acceleration = physics::boost_acceleration_low;
		else if (speed > physics::boosting_speed * get_velocity_multiplier())
			d.base_acceleration = -physics::deceleration_medium;
		else
			d.base_acceleration = 0.0f;
	}
	else if (d.boost > 0.0f && d.boost_held && d.is_on_ground)
	{
		d.is_using_boost = true;
		d.boost_cooldown = 0.5f;
		d.boost = std::max(0.0f, d.boost - physics::boost_consumption_rate * d.delta);
		if (d.is_sliding)
			d.base_acceleration = -physics::slide_deceleration_low;
		else if (speed < physics::half_running_speed * get_velocity_multiplier())
			d.base_acceleration = physics::boost_acceleration_high * get_acceleration_multiplier();
		else if (speed < physics::running_speed * get_velocity_multiplier())
			d.base_acceleration = physics::boost_acceleration_medium * get_acceleration_multiplier();
		else if (speed < physics::boosting_speed * get_velocity_multiplier())
			d.base_acceleration = physics::boost_acceleration_low;
		else if (speed > physics::boosting_speed * get_velocity_multiplier())
			d.base_acceleration = -physics::deceleration_medium;
		else
			d.base_acceleration = 0.0f;
	}
	else if (!d.is_sliding || (m_grapple->d.is_connected && speed > physics::running_speed * get_velocity_multiplier()))
	{
		float num = d.collision_tangent.y > 0.0f ?
			1.0f + std::min(0.75f, 1.0f - vector{ (float)d.move_direction, 0.0f }.dot(d.collision_tangent)) :
			1.0f;

		if (speed > physics::boosting_speed * get_velocity_multiplier())
			d.base_acceleration = -physics::deceleration_high;
		else if (speed > physics::running_speed * num * get_velocity_multiplier())
			d.base_acceleration = -physics::deceleration_medium;
		else if (speed > physics::half_running_speed * get_velocity_multiplier())
			d.base_acceleration = physics::acceleration_low * get_acceleration_multiplier();
		else
			d.base_acceleration = physics::acceleration_high * get_acceleration_multiplier();
	}
	else if (m_grapple->d.is_connected) 
	{
		d.base_acceleration = 0.0f;
	}
	else if (d.is_sliding)
	{
		float num = d.collision_tangent.y > 0.0f ?
			1.0f + std::min(0.75f, 1.0f - vector{ (float)d.move_direction, 0.0f }.dot(d.collision_tangent)) :
			1.0f;

		if (speed > physics::running_speed * num * get_velocity_multiplier())
			d.base_acceleration = -physics::slide_deceleration_high;
		else
			d.base_acceleration = -physics::slide_deceleration_medium;
	}
	else
		d.base_acceleration = physics::acceleration_low;

	if (d.is_in_air && !m_grapple->d.is_connected)
	{
		if (d.boost_cooldown > 0.0f && std::abs(velocity.x) > physics::running_speed * get_velocity_multiplier() && !d.is_using_boost && !d.is_using_drill)
			d.base_acceleration = -physics::deceleration_medium;
		else if (d.wall_jump_bonus_timer > 0.0f)
			if (std::abs(velocity.x) < physics::running_speed * 0.8f && ((velocity.x > 0.0f && d.right_held) || (velocity.x < 0.0f && d.left_held)))
				d.base_acceleration = physics::wall_jump_bonus_acceleration;
			else
				d.base_acceleration = 0.0f;
		else if (d.boost > 0.0f && d.boost_held)
		{
			d.is_using_boost = true;
			d.boost_cooldown = 0.5f;
			d.boost = std::max(0.0f, d.boost - physics::boost_consumption_rate * d.delta);
			if (std::abs(velocity.x) < physics::running_speed * get_velocity_multiplier())
				d.base_acceleration = physics::boost_acceleration_medium;
			else if (std::abs(velocity.x) < physics::boosting_speed * get_velocity_multiplier())
				d.base_acceleration = physics::boost_acceleration_low;
			else if ((d.right_held && !d.left_held && velocity.x < 0.0f) || (d.left_held && !d.right_held && velocity.x > 0.0f))
				d.base_acceleration = physics::boost_acceleration_low;
			else
				d.base_acceleration = 0.0f;
		}
		else if (d.is_using_drill)
		{
			if (std::abs(velocity.x) < physics::running_speed * get_velocity_multiplier())
				d.base_acceleration = physics::boost_acceleration_medium;
			else if (std::abs(velocity.x) < physics::boosting_speed * get_velocity_multiplier())
				d.base_acceleration = physics::boost_acceleration_low;
			else
				d.base_acceleration = 0.0f;
		}
		else if (std::abs(velocity.x) < physics::running_speed * 0.8f || (velocity.x > 0.0f && d.left_held && !d.right_held) || (velocity.x < 0.0f && d.right_held && !d.left_held))
			d.base_acceleration = physics::air_acceleration;
		else
			d.base_acceleration = 0.0f;
	}
	if (d.is_hooked2)
	{
		d.base_acceleration = -physics::hooked_deceleration;
	}
}

void player::get_grapple_position_and_direction(vector& position, vector& direction)
{
	int32_t num = d.move_direction;
	if (d.left_held && !d.right_held)
		num = -1;
	else if (d.right_held && !d.left_held)
		num = 1;
	else if (d.right_held && d.left_held && d.dominating_direction != 0)
		num = d.dominating_direction;
	position = get_collision()->get_vertex((num == 1) ? 0 : 1);
	direction.x = ((num == 1) ? 0.707f : -0.707f);
	direction.y = -0.707f;
}

void player::shoot_grapple(timespan time, vector position, vector direction)
{
	// ignore irrelevant code
	d.is_grappling = true;
	d.grapple_time = time;
	get_off_wall(time);
	m_grapple->shoot(position, direction);
	// ignore graphics code
}

void player::connect_grapple(vector grapple_position, float radius, int direction)
{
	m_grapple->connect(grapple_position);
	// ignore grapple rope code
	d.swing_radius = radius;
	d.move_direction = direction;
	d.is_swinging = true;
	reset_jump();
	d.is_in_air = true;
	d.jump_state = 1;
	// ignore irrelevant code
}

void player::update_jump(timespan time, timespan delta, vector& velocity, timespan jump_time)
{
	if ((time - jump_time - delta).seconds() > d.jump_duration)
		return;
	float num1 = std::min(d.jump_duration, (time - jump_time).seconds_f());
	float num2 = 0.25f / d.jump_duration;
	float num3 = std::max(0.0f, (0.25f - (time - jump_time).seconds_f())) / d.jump_duration;
	float num4 = num1 * num3;
	float num5 = num1 * (num2 - num3) / 2.0f;
	float scale = num4 + num5;
	d.jump_velocity = d.initial_jump_velocity + d.jump_direction * d.jump_strength * scale;
	if (d.jump_velocity.y < velocity.y)
	{
		if (std::abs(velocity.y) > std::abs(d.jump_velocity.y))
		{
			vector dir = vector{ velocity.x, d.jump_velocity.y }.normalized();
			float speed = velocity.length();
			if (speed > physics::boosting_speed)
				speed = (speed + physics::boosting_speed) / 2.0f;
			velocity = speed * dir;
			return;
		}
		velocity.y = d.jump_velocity.y;
	}
}

void player::reset_jump()
{
 	d.is_in_air = false;
	d.next_jump_state = 1;
	d.jump_state = 0;
	d.jump_velocity = vec_zero;
	d.jump_strength = physics::jump_strength;
	d.jump_direction = vec_up;
	d.can_buffer_wall_jump = false;
	if (vector{ (float)d.move_direction, 0.0f }.dot(d.collision_tangent) <= 0.0f)
		d.collision_tangent = -d.collision_tangent;
	if (d.is_on_ground)
	{
		if (d.collision_tangent.y == 0.0f)
		{
			m_actor->d.velocity = std::abs(m_actor->d.velocity.x) * d.collision_tangent;
			return;
		}
		m_actor->d.velocity = m_actor->d.velocity.length() * d.collision_tangent;
	}
}

void player::cancel_grapple()
{
	d.is_grappling = false;
	d.is_swinging = false;
	d.can_grapple = false;
	// ignore grappple rope code
	m_grapple->cancel();
	if (m_actor->d.velocity.x > 0.0f)
		d.move_direction = 1;
	else if (m_actor->d.velocity.x < 0.0f)
		d.move_direction = -1;
}

void player::get_off_wall(timespan time)
{
	if (d.is_climbing)
	{
		d.is_climbing = false;
		d.wall_get_off_time = time;
		m_actor->d.velocity = vector{ m_actor->d.velocity.x * -100.0f, 0.0f };
		d.move_direction = m_actor->d.velocity.x > 0.0f ? 1 : -1;
	}
}

void player::unfreeze()
{
	if (d.is_frozen)
	{
		d.is_frozen = false;
		d.frozen_timer = 0.0f;
	}
}

void player::hit_obstacle(timespan time, vector velocity)
{
	if (!d.is_using_drill && (time - d.stumble_time).seconds() > physics::stumble_duration)
	{
		if (d.is_frozen)
		{
			m_actor->d.velocity = vector{ m_actor->d.velocity.x * 0.6f, velocity.y };
		}
		else
		{
			float x = (float)(m_actor->d.velocity.x > 0.0f ? 1 : -1) * std::max(std::abs(m_actor->d.velocity.x) * 0.2f, std::abs(m_actor->d.velocity.x) - physics::running_speed);
			m_actor->d.velocity = vector{ x, velocity.y };
		}
		d.stumble_time = time;
		d.is_stumbling = true;
		cancel_grapple();
		bool was_in_air = d.is_in_air;
		reset_jump();
		if (was_in_air)
		{
			d.jump_state = 2;
			d.next_jump_state = 2;
			d.is_in_air = true;
		}
		d.is_taunting = false;
	}
}

bool player::is_solid(i_collidable* a1) const
{
	collidable_type collidable_type = a1->get_collidable_type();
	if (collidable_type >= 0 && collidable_type < 100)
		return true;
	// ignore unused code
	switch (collidable_type)
	{
	case col_fall_tile:
		return a1->get_collision() != nullptr;
	case col_saw:
	case col_obstacle:
	case col_dropped_obstacle:
		return !d.is_using_drill;
	case col_switch_block:
	case col_moving_platform:
		return true;
	default:
		return false;
	}
}

void player::unknown3(timespan time, i_collidable* a1)
{
	// todo
	unknown4(time, a1);
}

void player::unknown4(timespan time, i_collidable* a1)
{
	collidable_type collidable_type = a1->get_collidable_type();
	switch (collidable_type)
	{
	case col_obstacle:
		static_cast<obstacle*>(static_cast<actor*>(a1)->m_controller.get())->break_(time, m_actor->d.position);
		hit_obstacle(time, vec_zero);
		break;
	default:
		break;
	}

	// TODO
}

void player::unknown11(timespan time, i_collidable* a1)
{
	collidable_type collidable_type = a1->get_collidable_type();
	if (collidable_type != col_wall_left)
	{
		if (collidable_type != col_wall_right)
		{
			if (collidable_type != col_switch_block)
				unknown4(time, a1);
			else
			{
				if (can_climb(a1))
				{
					attach_wall(time, a1->get_collision()->get_center().x < m_actor->d.position.x ? 1 : -1, a1);
					return;
				}
				unknown4(time, a1);
				return;
			}
		}
		else if (can_climb(a1))
		{
			attach_wall(time, 1, a1);
			return;
		}
	}
	else if (can_climb(a1))
	{
		attach_wall(time, -1, a1);
		return;
	}
}

void player::check_collision(timespan time, timespan delta)
{
	d.is_on_wall = false;
	d.is_ceiling_hit = false;
	bool was_on_ground = d.is_on_ground;
	d.is_on_ground = false;
	d.is_colliding_with_solid = false;
	// ignore seemingly unused code
	vector vertex_bl = get_collision()->get_vertex(2); // bottom-left
	vector vertex_br = get_collision()->get_vertex(3); // bottom-right
	vector vertex_tl = get_collision()->get_vertex(1); // top-left
	vector vertex_tr = get_collision()->get_vertex(0); // top-right
	vector offset = vec_down; // offset for trace start and end
	vector t_start; // trace start
	vector t_end; // trace end
	int32_t col_count; // collision count for trace

	auto& collision_pairs = caches::inst.collision_pairs_ptr;

	// on ground check (below player)
	if (d.jump_time.seconds() < time.seconds() - physics::jump_duration)
	{
		t_start = vertex_bl + offset;
		t_end = vertex_br + offset;

		col_count = m_actor->m_state->m_collision_engine.trace(t_start, t_end, d.collision_filter1, collision_pairs);
		for (int32_t i = 0; i < col_count; i++)
		{
			collision_pair& collision_pair = *collision_pairs[i];
			if (collision_pair.m_is_colliding && is_solid(collision_pair.m_target))
			{
				// ignore particle code
				d.is_on_ground = true;
				// ignore seemingly unused code
				d.is_on_fall_tile = (collision_pair.m_target->get_collidable_type() == col_fall_tile);
				d.is_climbing_up = false;
				d.unused2 = false;
				d.unused3 = false;
				break;
			}
		}
	}

	if (!d.is_in_air && !d.is_swinging && !(d.is_inside_super_boost || d.is_inside_super_boost_wind || d.bounce_pad_timer > 0.0f) && was_on_ground && !d.is_on_ground && (!d.is_shooting || false) && !d.is_hooked2 && !d.is_stunned) // todo
	{
		if (d.displacement.x > 0.0f)
			t_start = vertex_br;
		else
			t_start = vertex_bl;

		t_end = t_start + vec_down * (std::abs(d.displacement.y) + 1.0f);
		col_count = m_actor->m_state->m_collision_engine.trace(t_start, t_end, d.collision_filter1, collision_pairs);
		i_collidable* closest_col = nullptr;
		float min_dist = FLOAT_MAX;
		for (int32_t i = 0; i < col_count; i++)
		{
			collision_pair& collision_pair = *collision_pairs[i];
			float dist = (collision_pair.m_target->get_collision()->get_position() - m_actor->d.position).length_sqr();
			int32_t col_type = collision_pair.m_target->get_collidable_type();
			if ((col_type == col_square || col_type == col_wall_right || col_type == col_wall_left || col_type == col_grapple_ceil) && dist < min_dist)
			{
				min_dist = dist;
				closest_col = collision_pair.m_target;
			}
		}
		if (closest_col != nullptr)
		{
			if (d.displacement.x < 0.0f)
			{
				float num = (m_actor->d.position - (closest_col->get_bounds().get_position() + vec_right * closest_col->get_bounds().width - vec_down * m_actor->get_size().y)).length();
				m_actor->set_position(vector{ closest_col->get_bounds().max_x + vec_left.x * num, closest_col->get_bounds().min_y - m_actor->get_bounds().height });
				d.ground_normal = vec_up;
				d.collision_tangent = vec_left;
				m_actor->d.velocity = d.collision_tangent * m_actor->d.velocity.length();
				d.is_on_ground = true;
			}
			else
			{
				float num = (m_actor->d.position - (closest_col->get_bounds().get_position() - m_actor->get_size())).length();
				m_actor->set_position(vector{ closest_col->get_bounds().min_x - m_actor->get_size().x + vec_right.x * num, closest_col->get_bounds().min_y - m_actor->get_bounds().height });
				d.ground_normal = vec_up;
				d.collision_tangent = vec_right;
				m_actor->d.velocity = d.collision_tangent * m_actor->d.velocity.length();
				d.is_on_ground = true;
			}
		}
	}
	if (!d.is_in_air && !d.is_swinging && !(d.is_inside_super_boost || d.is_inside_super_boost_wind || d.bounce_pad_timer > 0.0f) && was_on_ground && !d.is_on_ground && !d.is_hooked)
	{
		if (d.displacement.x > 0.0f)
			t_start = vertex_bl;
		else
			t_start = vertex_br;

		t_end = t_start + vec_down * (std::abs(d.displacement.x) + 1.0f);
		col_count = m_actor->m_state->m_collision_engine.trace(t_start, t_end, d.collision_filter1, collision_pairs);
		for (int32_t i = 0; i < col_count; i++)
		{
			i_collidable* target = collision_pairs[i]->m_target;
			if (target->get_collidable_type() == col_slope_floor_left)
			{
				float num = m_actor->d.position.x - target->get_bounds().min_x;
				m_actor->set_position(vector{ target->get_bounds().min_x + vec_down_right.x * num, target->get_bounds().min_y + vec_down_right.y * num - m_actor->get_bounds().height - 2.5E-05f });
				d.ground_normal = vec_up_right;
				d.collision_tangent = vec_down_right;
				m_actor->d.velocity = d.collision_tangent * m_actor->d.velocity.length();
				d.is_on_ground = true;
				break;
			}
			if (target->get_collidable_type() == col_slope_floor_right)
			{
				float num = target->get_bounds().max_x - m_actor->d.position.x - m_actor->get_bounds().width;
				m_actor->set_position(vector{ target->get_bounds().max_x + vec_down_left.x * num - m_actor->get_bounds().width, target->get_bounds().min_y + vec_down_left.y * num - m_actor->get_bounds().height - 2.5E-05f });
				d.ground_normal = vec_up_left;
				d.collision_tangent = vec_down_left;
				m_actor->d.velocity = d.collision_tangent * m_actor->d.velocity.length();
				d.is_on_ground = true;
				break;
			}
		}
	}
	offset = vec_down * 2.0f;
	bool flag2 = true;
	int num2 = (d.move_direction == 1) ? 3 : 2;
	if (num2 == 3)
	{
		t_start = vertex_br + vec_up * 5.0f;
		t_end = vertex_br + vec_down * 2.0f;
	}
	else
	{
		t_start = vertex_bl + vec_up * 5.0f;
		t_end = vertex_bl + vec_down * 2.0f;
	}
	col_count = m_actor->m_state->m_collision_engine.trace(t_start, t_end, d.collision_filter1, collision_pairs);
	d.ground_collidable_type = col_air;
	for (int32_t i = 0; i < col_count; i++)
	{
		collision_pair& collision_pair = *collision_pairs[i];
		if (collision_pair.m_is_colliding)
		{
			if (tile_actor* tile_actor = dynamic_cast<::tile_actor*>(collision_pair.m_target))
			{
				collidable_type col_type = tile_actor->get_collidable_type();
				if ((d.ground_collidable_type == col_air || (!(col_type == col_square || col_type == col_wall_right || col_type == col_wall_left || col_type == col_grapple_ceil) && m_actor->d.velocity.y > 0.0f)) && col_type != col_slope_ceil_right && col_type != col_slope_ceil_left)
				{
					m_unknown13 = collision_pair.m_target;
					d.ground_collidable_type = col_type;
				}
			}
			else
			{
				if (is_solid(collision_pair.m_target))
				{
					flag2 = false;
					m_unknown13 = collision_pair.m_target;
					d.ground_collidable_type = collision_pair.m_target->get_collidable_type();
					unknown3(time, collision_pair.m_target);
					break;
				}
				unknown3(time, collision_pair.m_target);
			}
		}
	}
	if (flag2)
	{
		if (num2 == 2)
		{
			t_start = vertex_br + vec_up * 5.0f;
			t_end = vertex_br + vec_down * 2.0f;
		}
		else
		{
			t_start = vertex_bl + vec_up * 5.0f;
			t_end = vertex_bl + vec_down * 2.0f;
		}
		col_count = m_actor->m_state->m_collision_engine.trace(t_start, t_end, d.collision_filter1, collision_pairs);
		for (int32_t i = 0; i < col_count; i++)
		{
			collision_pair& collision_pair = *collision_pairs[i];
			if (collision_pair.m_is_colliding)
			{
				if (tile_actor* tile_actor = dynamic_cast<::tile_actor*>(collision_pair.m_target))
				{
					collidable_type col_type = tile_actor->get_collidable_type();
					if ((d.ground_collidable_type == col_air || (!(col_type == col_square || col_type == col_wall_right || col_type == col_wall_left || col_type == col_grapple_ceil) && m_actor->d.velocity.y > 0.0f)) && col_type != col_slope_ceil_right && col_type != col_slope_ceil_left)
					{
						m_unknown13 = collision_pair.m_target;
						d.ground_collidable_type = col_type;
					}
				}
				else
				{
					if (is_solid(collision_pair.m_target))
					{
						m_unknown13 = collision_pair.m_target;
						d.ground_collidable_type = collision_pair.m_target->get_collidable_type();
						unknown3(time, collision_pair.m_target);
						break;
					}
					unknown3(time, collision_pair.m_target);
				}
			}
		}
	}
	t_start = vertex_tl + vec_up;
	t_end = vertex_tr + vec_up;
	col_count = m_actor->m_state->m_collision_engine.trace(t_start, t_end, d.collision_filter2, collision_pairs);
	for (int32_t i = 0; i < col_count; i++)
	{
		collision_pair& collision_pair = *collision_pairs[i];
		if (collision_pair.m_is_colliding)
		{
			if (!d.is_ceiling_hit && is_solid(collision_pair.m_target))
			{
				if (m_actor->d.velocity.y < 0.0f)
				{
					m_actor->d.velocity.y = 0.0f;
				}
				d.is_ceiling_hit = true;
			}
			unknown4(time, collision_pair.m_target);
		}
	}
	if (m_actor->d.velocity.x > 0.0f)
	{
		t_start = vertex_tr;
		t_end = vertex_br;
		t_start.x += 5.0f;
	}
	else if (m_actor->d.velocity.x < 0.0f)
	{
		t_start = vertex_tl;
		t_end = vertex_bl;
		t_start.x += -5.0f;
	}
	else if (d.right_held)
	{
		t_start = vertex_tr;
		t_end = vertex_br;
		t_start.x += 5.0f;
	}
	else if (d.left_held)
	{
		t_start = vertex_tl;
		t_end = vertex_bl;
		t_start.x += -5.0f;
	}
	t_end.x = t_start.x;
	t_end.y += -1.0f;
	col_count = m_actor->m_state->m_collision_engine.trace(t_start, t_end, d.collision_filter2, collision_pairs);
	for (int32_t i = 0; i < col_count; i++)
	{
		collision_pair& collision_pair = *collision_pairs[i];
		if (collision_pair.m_is_colliding)
		{
			if (!d.is_colliding_with_solid && is_solid(collision_pair.m_target))
			{
				if (d.is_grappling)
				{
					if (m_actor->d.velocity.x * m_grapple->d.direction.x > 0.0f || collision_pair.m_target->get_collidable_type() <= 0 || collision_pair.m_target->get_collidable_type() >= 100)
					{
						d.is_colliding_with_solid = true;
						d.solid_collision_mtd = collision_pair.m_mtd;
					}
				}
				else
				{
					d.is_colliding_with_solid = true;
					d.solid_collision_mtd = collision_pair.m_mtd;
				}
			}
			unknown11(time, collision_pair.m_target);
		}
	}
	if (d.is_sliding || d.is_using_drill)
	{
		if (m_actor->d.velocity.x >= 0.0f)
		{
			t_start = vertex_tr;
			t_end = vertex_br;
		}
		else
		{
			t_start = vertex_tl;
			t_end = vertex_bl;
		}
		t_end.y += -1.0f;
		col_count = m_actor->m_state->m_collision_engine.trace(t_start, t_end, d.collision_filter2, collision_pairs);
		for (int32_t i = 0; i < col_count; i++)
		{
			collision_pair& collision_pair = *collision_pairs[i];
			if (collision_pair.m_is_colliding && collision_pair.m_target->get_collidable_type() == col_player)
			{
				// todo
			}
		}
	}
	col_count = m_actor->m_state->m_collision_engine.check_shape(m_actor->get_bounds(), *m_actor->get_collision(), d.collision_filter2, collision_pairs);
	for (int32_t i = 0; i < col_count; i++)
	{
		collision_pair& collision_pair = *collision_pairs[i];
		if (collision_pair.m_is_colliding)
		{
			unknown4(time, collision_pair.m_target);
		}
	}
	if (d.unused2 || (!d.is_on_ground && (d.is_colliding_with_solid || d.is_climbing) && ((d.left_held && d.move_direction == -1) || (d.right_held && d.move_direction == 1))))
	{
		if (d.move_direction >= 0)
			t_start = vertex_br;
		else
			t_start = vertex_bl;

		t_start.y -= 20.0f;
		t_end = t_start;
		t_end.x += (float)(50 * d.move_direction);
		bool flag3 = !d.is_climbing || !d.unused2;
		if (m_actor->d.velocity.x == 0.0f && m_actor->d.velocity.y > 0.0f && !d.is_climbing_up && d.is_sliding && (float)d.move_direction * d.solid_collision_mtd.x >= 0.0f)
			flag3 = false;
		col_count = m_actor->m_state->m_collision_engine.trace(t_start, t_end, d.collision_filter2, collision_pairs);
		if (flag3)
		{
			for (int32_t i = 0; i < col_count; i++)
			{
				collision_pair& collision_pair = *collision_pairs[i];
				if (is_solid(collision_pair.m_target))
				{
					flag3 = false;
					d.is_climbing_up = false;
					break;
				}
			}
		}
		if (flag3)
		{
			t_start = t_end;
			t_end.y -= 2.0f * 20.0f;
			col_count = m_actor->m_state->m_collision_engine.trace(t_start, t_end, d.collision_filter2, collision_pairs);
			for (int32_t i = 0; i < col_count; i++)
			{
				collision_pair& collision_pair = *collision_pairs[i];
				if (is_solid(collision_pair.m_target))
				{
					flag3 = false;
					d.is_climbing_up = false;
					break;
				}
			}
		}
		if (d.move_direction >= 0)
			t_start = vertex_br;
		else
			t_start = vertex_bl;
		t_start.y -= 65.0f;
		t_start.x += (float)(d.move_direction * 20);
		t_end = t_start;
		t_end.y -= 50.0f;
		col_count = m_actor->m_state->m_collision_engine.trace(t_start, t_end, d.collision_filter2, collision_pairs);
		d.can_climb_up = true;
		for (int32_t i = 0; i < col_count; i++)
		{
			collision_pair& collision_pair = *collision_pairs[i];
			if (is_solid(collision_pair.m_target))
			{
				d.can_climb_up = false;
				break;
			}
		}
		if (flag3 && d.can_climb_up && !d.unused2 && !d.unused3)
		{
			d.is_climbing_up = true;
		}
		if (d.unused2 && flag3)
		{
			d.unused2 = false;
			m_actor->d.velocity = vector{ (float)d.move_direction * physics::unused, -physics::unused };
			d.unused3 = true;
			return;
		}
	}
	else if (!d.is_colliding_with_solid && !d.is_climbing)
	{
		d.is_climbing_up = false;
		d.unused2 = false;
		d.can_climb_up = false;
		d.unused3 = false;
	}
}

void player::update_ground_normal()
{
	collidable_type num = d.ground_collidable_type;
	switch (num)
	{
	case col_square:
	case col_grapple_ceil:
	case col_wall_left:
	case col_wall_right:
		d.ground_normal = vec_up;
		return;
	case col_slope_ceil_left:
	case col_slope_ceil_right:
		break;
	case col_slope_floor_left:
		d.ground_normal = vec_up_right;
		return;
	case col_slope_floor_right:
		d.ground_normal = vec_up_left;
		return;
	default:
		if (num == col_switch_block)
		{
			actor* actor = static_cast<::actor*>(m_unknown13);
			float num2 = actor->get_collision()->get_rotation();
			if (num2 >= 0.0f)
			{
				num2 = (float)mod(actor->get_collision()->get_rotation(), 6.283185307179586);
			}
			else
			{
				num2 = (float)(6.283185307179586 - mod(std::abs(actor->get_collision()->get_rotation()), 6.283185307179586));
			}
			if (num2 < 1.5707963267948966 || num2 > 4.71238898038469)
			{
				num2 += 3.1415927f;
			}
			num2 += 1.5707964f;
			d.ground_normal.x = (float)std::cos((double)num2);
			d.ground_normal.y = (float)std::sin((double)num2);
			return;
		}
		break;
	}
	d.ground_normal = vec_up;
}

void player::unknown6()
{
	if ((d.move_direction == 1 && d.ground_normal == vec_down_right) || (d.move_direction == -1 && d.ground_normal == vec_down_left))
		return;
	if (d.is_inside_super_boost || d.is_inside_super_boost_wind || d.bounce_pad_timer > 0.0f)
	{
		d.collision_tangent = d.super_boost_direction;
		return;
	}
	d.collision_tangent.x = -d.ground_normal.y;
	d.collision_tangent.y = d.ground_normal.x;
	if (d.is_in_air && d.is_on_ground)
	{
 		if (std::abs(d.collision_tangent.x) != 1.0f && std::abs(d.collision_tangent.y) != 1.0f)
		{
			if (d.collision_tangent.y < 0.0f)
				d.collision_tangent = -d.collision_tangent;
			float num = m_actor->d.velocity.normalized().dot(d.collision_tangent);
			if (num < 0.0f)
			{
				d.collision_tangent = -d.collision_tangent;
				if (num < -0.707f)
				{
					m_actor->d.velocity = d.collision_tangent * m_actor->d.velocity.length();
				}
				else
				{
					float value = std::abs(m_actor->d.velocity.x);
					float value2 = m_actor->d.velocity.length();
					float scale_factor = lerp(value, value2, num / -0.707f);
					m_actor->d.velocity = d.collision_tangent * scale_factor;
				}
			}
			else if (num < 0.707f)
			{
				if (vector{ d.right_held ? 1.0f : (d.left_held ? -1.0f : ((m_actor->d.velocity.x > 0.0f) ? 1.0f : -1.0f)), 0.0f }.dot(d.collision_tangent) > 0.0f)
				{
					m_actor->d.velocity = d.collision_tangent * m_actor->d.velocity.length();
				}
				else
				{
					d.collision_tangent = -d.collision_tangent;
					m_actor->d.velocity = d.collision_tangent * std::abs(m_actor->d.velocity.x);
				}
			}
			else
			{
				m_actor->d.velocity = d.collision_tangent * m_actor->d.velocity.length();
			}
		}
		else
		{
			if (std::abs(d.collision_tangent.x) == 1.0f)
			{
				m_actor->d.velocity.y = 0.0f;
			}
			else
			{
				m_actor->d.velocity = d.collision_tangent * m_actor->d.velocity.length();
			}
		}
		if (!d.is_swinging && m_actor->d.velocity.x != 0.0f)
			d.move_direction = ((m_actor->d.velocity.x > 0.0f) ? 1 : -1);
		reset_jump();
		return;
	}
	if (!d.is_hooked)
	{
		if (vector{ (float)d.move_direction, 0.0f }.dot(d.collision_tangent) <= 0.0f)
		{
			d.collision_tangent = -d.collision_tangent;
		}
		if (d.ground_collidable_type != col_air && !d.is_in_air && !d.is_dying2 && !d.is_climbing && !d.is_stunned)
		{
			float num2 = m_actor->d.velocity.length();
			if (num2 > 0.0f)
			{
				m_actor->d.velocity = d.collision_tangent * num2;
			}
		}
	}
}

void player::resolve_object_collisions(int max_iteration)
{
	const collision_pair* collision = m_actor->get_collision(0);
	int32_t num = 0;
	while (num < m_actor->get_collision_count() && collision->m_iteration <= max_iteration)
	{
		collision = m_actor->get_collision(num);
		if (collision->m_is_colliding && !dynamic_cast<tile_actor*>(collision->m_target))
			resolve_object_collision(collision->m_target);
		num++;
	}
}

void player::resolve_object_collision(i_collidable* a1)
{
	int32_t collidable_type = a1->get_collidable_type();
	switch (collidable_type)
	{
	case col_finish_trigger:
		d.has_touched_finish_bomb = true;
		break;
	case col_boost_section:
	{
		if (d.is_inside_boost_section)
			break;
		d.is_inside_boost_section = true;
		float num = (d.player_game_mode == 0 || d.player_game_mode == 5) ? 3.0f : 1.0f;
		d.boost += physics::boost_charge_rate * num * d.delta;
		d.boost = std::min(d.skin_id == 4 ? 1.0f : 2.0f, d.boost);
		// ignore graphics and sound code
		d.was_inside_boost_section = true;
		break;
	}
	case col_super_boost:
		super_boost_volume* super_boost_volume = static_cast<emu::super_boost_volume*>(static_cast<actor*>(a1)->m_controller.get());
		// ignore sound code
		d.super_boost_force = super_boost_volume->get_direction_vector() * physics::super_boost_speed;
		d.super_boost_direction = super_boost_volume->get_direction_vector();
		d.jump_state = 0;
		d.is_inside_super_boost = true;
		if (d.is_stunned)
		{
			d.is_stunned = false;
			d.stunned_by_id = INT32_MIN;
		}
	}
}

void player::resolve_collision_implementation(timespan time)
{
	d.previous_position = m_actor->d.position;
	vector zero = vec_zero;
	float num = 0.0f;
	vector pos = m_actor->d.position;
	// ignore error handling code
	m_actor->set_position(m_actor->d.position + m_actor->d.velocity * m_actor->d.iteration_time_step);
	float num2 = m_actor->d.velocity.length();
	int num3 = 0;
	vector zero2 = vec_zero;
	vector zero3 = vec_zero;
	resolve_object_collisions(0);
	for (int32_t i = 1; i < m_actor->d.total_iterations; i++)
	{
		collision_pair collision_pair;
		bool has_collision_pair = false;
		if (any_collision_with_solid(i, zero))
		{
			int32_t j = 0;
			while (j < 5)
			{
				j++;
				update_hitboxes();
				m_actor->update_collision();
				if (!unknown9(0, 0))
					break;
				float num4 = FLOAT_MIN;
				for (int32_t k = 0; k < m_actor->get_collision_count(); k++)
				{
					::collision_pair* collision = m_actor->get_collision(k);
					if (collision->m_is_colliding && is_solid(collision->m_target))
					{
						float num5 = zero.dot(collision->m_mtd.normalized());
						if (num5 > num4)
						{
							collision_pair = *collision;
							has_collision_pair = true;
							num4 = num5;
						}
					}
				}
				if (!has_collision_pair)
					break;
				if (num4 > 0.26f)
					m_actor->set_position(m_actor->d.position + collision_pair.m_mtd + collision_pair.m_mtd.normalized() * 0.1f);
				else if (dynamic_cast<tile_actor*>(collision_pair.m_target) && collision_pair.m_target->get_bounds().get_center().y > m_actor->get_bounds().get_center().y)
				{
					float num6 = collision_pair.m_target->get_bounds().min_y - m_actor->get_bounds().height;
					collision_pair.m_mtd.x = 0.0f;
					collision_pair.m_mtd.y = num6 - m_actor->d.position.y;
					m_actor->set_position(vector{ m_actor->d.position.x, num6 });
				}
				else
					m_actor->set_position(m_actor->d.position + zero);
			}
		}
		vector vec = m_actor->d.position - pos;
		float num7 = vec.length() / num2;
		num += num7;
		if (has_collision_pair)
		{
			collision_pair.find_blocking_axis_on_target(zero2, zero3);
			vector vec2 = zero3 - zero2;
			vec2 = vec2.normalized();
			float num8 = m_actor->d.velocity.normalized().dot(vec2);
			if (num8 < 0.0f)
			{
				vec2 *= -1.0f;
				num8 *= -1.0f;
			}
			d.collision_tangent = vec2;
			if (!d.is_on_ground && collision_pair.m_mtd.normalized().y > 0.5f)
			{
				if (std::abs(vec2.x) > 0.9f)
				{
					if (m_actor->d.velocity.y <= 0.0f)
					{
						m_actor->d.velocity.y = 0.0f;
					}
				}
				else if (m_actor->d.velocity.dot(vector(d.collision_tangent.x, -d.collision_tangent.y)) * d.collision_tangent.y > 0.0f)
				{
					m_actor->d.velocity = d.collision_tangent * m_actor->d.velocity.length();
					d.move_direction = ((m_actor->d.velocity.x >= 0.0f) ? 1 : -1);
				}
				d.jump_velocity.y = 0.0f;
				d.jump_time = 0;
			}
			else if (can_climb(collision_pair.m_target))
			{
				int num9 = 0;
				if (collision_pair.m_target->get_collision()->get_center().x - get_collision()->get_center().x > 0.0f)
					num9 = -1;
				else if (collision_pair.m_target->get_collision()->get_center().x - get_collision()->get_center().x < 0.0f)
					num9 = 1;
				attach_wall(time, num9, collision_pair.m_target);
			}
			else if (!d.is_on_ground && std::abs(d.collision_tangent.x) == 1.0f && m_actor->d.velocity.y > 0.0f)
			{
				m_actor->d.velocity.y = 0.0f;
			}
			else if (std::abs(d.collision_tangent.y) == 1.0f && (collision_pair.m_target->get_collision()->get_center().x - get_collision()->get_center().x) * m_actor->d.velocity.x > 0.0f)
			{
				if (m_grapple->d.is_connected && m_actor->d.velocity.x * (float)d.move_direction > 0.0f)
				{
					cancel_grapple();
				}
				m_actor->d.velocity.x = 0.0f;
			}
		}
		update_hitboxes();
		if (num2 == 0.0f || vec.length_sqr() == 0.0f || d.delta - num < 0.0001f)
			break;
		num3++;
		if (num3 > 10)
			break;
		pos = m_actor->d.position;
		m_actor->update_collision(d.delta - num);
		m_actor->set_position(m_actor->d.position + m_actor->d.velocity * m_actor->d.iteration_time_step);
		i = 0;
	}
	d.displacement = m_actor->d.position - d.previous_position;
	d.travel_distance += d.displacement.length() / 30.0f;
}

bool player::any_collision_with_solid(int32_t a1, vector& a2)
{
	a2.x = 0.0f;
	a2.y = 0.0f;
	bool flag = false;
	vector center = m_actor->get_collision()->get_center();
	collision_pair* collision = m_actor->get_collision(0);
	int32_t num = 0;
	while (num < m_actor->get_collision_count() && collision->m_iteration <= a1)
	{
		collision = m_actor->get_collision(num);
		if (collision->m_is_colliding)
		{
			if (collision->m_iteration == a1 && is_solid(collision->m_target))
			{
				a2 += (center - collision->m_target->get_collision()->get_center()).normalized();
				flag = true;
			}
			if (!dynamic_cast<tile_actor*>(collision->m_target))
			{
				resolve_object_collision(collision->m_target);
			}
		}
		num++;
	}
	if (flag)
	{
		a2 = a2.normalized();
	}
	return flag;
}

bool player::unknown9(int32_t a1, int32_t a2)
{
	collision_pair* collision = m_actor->get_collision(a2);
	int32_t num = a2;
	while (num < m_actor->get_collision_count() && collision->m_iteration <= a1)
	{
		collision = m_actor->get_collision(num);
		if (collision->m_iteration == a1 && collision->m_is_colliding && is_solid(collision->m_target))
			return true;
		num++;
	}
	return false;
}

bool player::can_climb(i_collidable* a1)
{
	return
		(a1->get_collidable_type() == col_wall_right && a1->get_collision()->get_center().x - get_collision()->get_center().x < 0.0f) ||
		(a1->get_collidable_type() == col_wall_left && a1->get_collision()->get_center().x - get_collision()->get_center().x > 0.0f) ||
		(a1->get_collidable_type() == col_switch_block && std::abs(std::cos((double)a1->get_collision()->get_rotation() - 1.5707963267948966)) > 0.9998999834060669 && m_actor->d.velocity.y <= 0.0f);
}

void player::attach_wall(timespan time, int32_t a1, i_collidable* a2)
{
	if (!d.enable_climbing || d.unused2 || d.is_climbing_up || d.is_hooked2 || d.is_dying || d.is_dying2 || (a1 == -1 && ((!d.is_climbing && m_actor->d.velocity.x < 0.0f) || a2->get_collision()->get_center().x - get_collision()->get_center().x < 0.0f)) || (a1 == 1 && ((!d.is_climbing && m_actor->d.velocity.x > 0.0f) || a2->get_collision()->get_center().x - get_collision()->get_center().x > 0.0f)) || time - d.wall_get_off_time < 2000000)
		return;
	if (d.is_frozen)
	{
		m_actor->d.velocity = vector{ 0.0f, m_actor->d.velocity.y };
		return;
	}
	if (d.is_on_ground && std::abs(m_actor->d.velocity.x) < 10.0f && d.is_climbing)
	{
		m_actor->d.velocity = vector(m_actor->d.velocity.x * -100.0f, 0.0f);
		d.is_climbing = false;
		d.move_direction = ((m_actor->d.velocity.x > 0.0f) ? 1 : -1);
		return;
	}
	d.is_on_wall = true;
	d.move_direction = -1 * a1;
	if (!d.is_climbing)
	{
		if (d.is_on_ground)
		{
			m_actor->d.velocity = vector{ 0.0f, -400.0f };
			d.is_on_ground = false;
			d.ground_collidable_type = col_air;
			// ignore seemingly irrelevant code
		}
		else if (m_actor->d.velocity.y < 100.0f)
		{
			float y = -1.0f * std::min(800.0f, m_actor->d.velocity.length());
			m_actor->d.velocity = vector{ (float)d.move_direction * 3.0f, y };
		}
		else
		{
			m_actor->d.velocity = vector{ (float)d.move_direction * 3.0f, m_actor->d.velocity.y * 0.5f };
		}
		if (d.can_buffer_wall_jump)
		{
			if ((d.move_direction == -1 && d.right_held) || (d.move_direction == 1 && d.left_held))
				d.can_repress_jump = true;
			// ignore graphics code
		}
		if (!d.is_climbing)
			reset_jump();
		// todo
		cancel_grapple();
	}
	d.is_climbing = true;
	d.can_climb_up = false;
	d.is_climbing_up = false;
	d.is_using_boost = false;
	d.wall_jump_bonus_timer = 0.35f;
}

void player::resolve_collision(timespan time, timespan delta)
{
	// todo
	d.delta = (float)delta.seconds();
	resolve_collision_implementation(time);
	update_hitboxes();
	// ignore graphics code
}

void player::unknown12()
{
	if (d.unknown22)
	{
		d.unknown23 -= d.delta;
		if (d.unknown23 <= 0.0f)
		{
			d.unknown23 = 0.0f;
			d.unknown22 = false;
		}
	}
}

void player::update_item(timespan time)
{
	if (d.item_cooldown > 0.0f)
		d.item_cooldown -= d.delta;
	if (d.stun_timer)
	{
		d.stun_timer -= d.delta;
		if (d.stun_timer <= 0.0f && d.is_stunned)
		{
			d.is_stunned = false;
			d.stunned_by_id = INT32_MAX;
		}
	}
	if (d.is_swap_item_cooldown_active)
	{
		d.swap_item_cooldown -= d.delta;
		if (d.swap_item_cooldown <= 0.0f)
			d.is_swap_item_cooldown_active = false;
	}
	// todo
	if (d.is_using_drill && (time - d.drill_time).seconds() > physics::drill_duration)
	{
		d.is_using_drill = false;
		// ignore graphics code
	}
	if (d.is_hooked2 || d.is_dying || d.is_dying2 || d.is_stunned || d.is_frozen)
		return;
	if (d.is_taunting && d.can_untaunt && (d.right_held || d.left_held || d.jump_held))
	{
		// ignore graphics code
		d.is_taunting = false;
	}
	if (d.taunt_held && !d.is_taunting && !d.is_using_drill)
	{
		d.is_taunting = true;
		d.can_untaunt = false;
		d.item_pressed = false;
		d.item_held = false;
		d.left_held = false;
		d.right_held = false;
		d.jump_held = false;
		// todo
		if (m_grapple != nullptr && (m_grapple->d.is_connected || d.is_grappling))
			cancel_grapple();
		get_off_wall(time);
		// ignore graphics code
	}
	// todo swap item stuff
	if (d.was_item_held && d.item_pressed)
		d.was_item_held = false;
	if (d.item_held && d.item_id == 14 && !d.is_taunting && !d.was_item_held)
	{
		if ((time - d.round_start_time).seconds() >= physics::round_start_item_cooldown && (d.item_id != 1 || !d.is_grappling))
			d.shockwave_charge = std::min(d.shockwave_charge + d.delta, 1.0f);
	}
	else if (d.item_pressed && d.item_id != 0 && !d.is_taunting)
	{
		if ((time - d.round_start_time).seconds() >= physics::round_start_item_cooldown && (d.item_id != 1 || !d.is_grappling))
		{
			// todo
		}
	}
	else if (!d.item_held && d.item_id == 14 && !d.is_taunting && d.shockwave_charge > 0.0f)
	{
		// todo
	}
	if (d.grapple_held && d.enable_grapples && d.can_grapple && !d.is_grappling && !d.is_shooting && !d.is_taunting)
	{
		// ignore irrelevant code
		vector position = vec_zero;
		vector direction = vec_zero;
		get_grapple_position_and_direction(position, direction);
		shoot_grapple(time, position, direction);
	}
	else if (d.is_grappling && (!d.grapple_held || !m_grapple->m_actor->d.is_collision_active || d.is_ceiling_hit || d.is_colliding_with_solid))
	{
		cancel_grapple();
	}
	// todo
	if (!d.grapple_held && (time - d.grapple_time).seconds() > physics::grapple_cooldown)
		d.can_grapple = true;
}

void player::update(timespan time, timespan delta)
{
	d.delta = (float)delta.seconds();
	if (d.item_has_hit && time > d.item_has_hit_time + d.unknown21 && !d.is_stunned)
		d.item_has_hit = false;

	// todo

	d.timer += d.delta;

	// ignore ghost code

	if (d.is_wrong_way && m_actor->d.velocity.length_sqr() > 25.0f)
		d.wrong_way_timer += d.delta;
	else
		d.wrong_way_timer = std::max(0.0f, d.wrong_way_timer - 2.0f * d.delta);

	bool was_left_held = d.left_held;
	bool was_right_held = d.right_held;
	bool was_item_held = d.item_held;
	d.was_jump_held = d.jump_held;
	d.left_held = m_actor->m_state->m_inputs[m_player_index][inp_left];
	d.right_held = m_actor->m_state->m_inputs[m_player_index][inp_right];
	d.jump_held = m_actor->m_state->m_inputs[m_player_index][inp_jump];
	d.grapple_held = m_actor->m_state->m_inputs[m_player_index][inp_grapple];
	d.slide_held = m_actor->m_state->m_inputs[m_player_index][inp_slide];
	d.boost_held = m_actor->m_state->m_inputs[m_player_index][inp_boost];
	d.item_held = m_actor->m_state->m_inputs[m_player_index][inp_item];
	d.item_pressed = d.item_held && !was_item_held;
	d.swap_item_held = m_actor->m_state->m_inputs[m_player_index][inp_swap_item];
	// Buffer a jump press that arrived while sliding — the firing block
	// further down ignores jump_held while is_sliding (when jump_state>=1),
	// so without this the press would be silently swallowed. The flag is
	// consumed inside the firing block once slide ends.
	if (d.is_sliding && d.jump_held && !d.was_jump_held)
		d.queued_jump = true;
	// Sticky "last pressed" direction while both keys are held. The grapple
	// firing code reads d.dominating_direction to pick its angle; making it
	// remember the most recent press (instead of resetting to 0 the frame
	// after the transition) means grapple keeps firing in the last-pressed
	// direction for as long as both keys stay down. Movement in that state
	// already cancels out via the sim's "exactly one held" gate, so the
	// player keeps facing the originally-held direction.
	if (d.left_held && d.right_held)
	{
		if (was_right_held && !was_left_held)
			d.dominating_direction = -1;
		else if (was_left_held && !was_right_held)
			d.dominating_direction = 1;
		// else: keep previous d.dominating_direction (sticky last-press).
	}
	else
	{
		d.dominating_direction = 0;
	}

	if (!d.is_dying)
	{
		// ignore empty function
		check_collision(time, delta);
		if (!d.is_on_wall && d.is_climbing)
		{
			d.is_climbing = false;
			d.wall_get_off_time = time;
			if (m_actor->d.velocity.y < 0.0f)
				m_actor->d.velocity.y = std::max(-300.0f, m_actor->d.velocity.y);
		}
		// ignore code that never gets called (likely handling some achievements)
		update_ground_normal();
		unknown6();
		update_item(time);
	}

	update_basic(time, delta);
	// ignore graphics code
	// todo
	if (d.was_slide_stopped)
		d.was_slide_stopped = false;
	// ignore graphics code
	d.prev_jump_state = d.jump_state;
	d.is_inside_super_boost = false;
	d.bounce_pad_timer = std::max(0.0f, d.bounce_pad_timer - d.delta);
	update_hitboxes();
	// todo
	if (!d.is_inside_boost_section)
		d.was_inside_boost_section = false;
	d.is_inside_boost_section = false;
	// todo
	unknown12();
	// ignore input code
}
