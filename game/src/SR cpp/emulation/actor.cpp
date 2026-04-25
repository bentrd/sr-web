#include "actor.h"
#include "collision_engine.h"
#include "state.h"

using namespace emu;

actor::actor(state* state, int32_t id, vector position, vector size) :
	m_state{ state }
{
	d.id = id;
	d.position = position;
	d.size = size;

	m_bounds = aabb{ d.position.x, d.position.x + d.size.x, d.position.y, d.position.y + d.size.y };
}

actor::actor(const actor& right) :
	d{ right.d },
	m_controller{ ::clone<i_actor_controller>(right.m_controller.get()) },
	m_state{ right.m_state },
	m_bounds{ right.m_bounds },
	m_quad_tree_parent{ right.m_quad_tree_parent }
{
	m_controller->set_actor(this);
	}

actor& actor::operator=(const actor& right)
{
	d = right.d;
	if (!m_controller->set(right.m_controller.get()))
	{
		m_controller = ::clone<i_actor_controller>(right.m_controller.get());
	}
	m_controller->set_actor(this);
	m_state = right.m_state;
	m_bounds = right.m_bounds;
	m_quad_tree_parent = right.m_quad_tree_parent;

	return *this;
}

std::unique_ptr<i_clonable> actor::clone() const
{
	return std::make_unique<actor>(*this);
}

void actor::set_controller(std::unique_ptr<i_actor_controller>&& controller)
{
	m_controller = std::move(controller);
	m_controller->set_actor(this);
	m_controller->init();
}

int32_t actor::get_id() const
{
	return d.id;
}

i_actor_controller* actor::get_controller()
{
	return m_controller.get();
}

vector actor::get_position() const
{
	return d.position;
}

void actor::set_position(vector position)
{
	// ignore error handling code
	if (d.position != position)
	{
		d.position_changed = true;
		d.position = position;
		m_bounds.set_position(d.position + d.unknown1); // not sure what exactly this is
	}
}

float actor::get_rotation() const
{
	return d.rotation;
}

void actor::set_rotation(float rotation)
{
	if (d.rotation != rotation)
	{
		d.rotation_changed = true;
		d.rotation = rotation;
		m_bounds.set_from_shape(*m_controller->get_collision());
	}
}

vector actor::get_size() const
{
	return d.size;
}

void actor::set_size(vector size)
{
	if (d.size != size)
	{
		d.size_changed = true;
		d.size = size;
		m_bounds.width = size.x;
		m_bounds.height = size.y;
		m_bounds.max_x = m_bounds.min_x + size.x;
		m_bounds.max_y = m_bounds.min_y + size.y;
	}
}

const aabb& actor::get_bounds() const
{
	return m_bounds;
}

vector actor::get_velocity() const
{
	return d.velocity;
}

void actor::set_velocity(vector velocity)
{
	d.velocity = velocity;
}

bool actor::is_moving() const
{
	return d.position_changed || d.rotation_changed || d.size_changed || d.velocity.x != 0.0f || d.velocity.y != 0.0f;
}

quad_tree_node* actor::get_quad_tree_parent() const
{
	return m_quad_tree_parent;
}

void actor::set_quad_tree_parent(quad_tree_node* parent)
{
	m_quad_tree_parent = parent;
}

i_collision_shape* actor::get_collision()
{
	return m_controller->get_collision();
}

collision_filter& actor::get_collision_filter()
{
	return m_controller->get_collision_filter();
}

collidable_type actor::get_collidable_type() const
{
	return m_controller->get_collidable_type();
}

collision_pair& actor::add_collision(i_collidable* source, i_collidable* target, vector source_pos, vector source_vel, vector target_pos, vector target_vel)
{
	return m_collisions.emplace_back(source, target, source_pos, source_vel, target_pos, target_vel);
}

collision_pair& actor::add_collision(collision_pair& other)
{
	m_collisions.push_back(other);
	return m_collisions.back();
}

int32_t actor::get_collision_count() const
{
	return (int32_t)m_collisions.size();
}

collision_pair* actor::get_collision(int32_t index)
{
	if (index < get_collision_count())
		return &m_collisions[index];
	return nullptr;
}

void actor::reset_collision(float delta, bool unused)
{
	// ignore error handling code
	m_collisions.clear();
	d.total_iterations = 1;
	d.iteration_time_step = delta;
	reset_bounds(delta);
}

void actor::reset_changed()
{
	d.position_changed = false;
	d.rotation_changed = false;
	d.size_changed = false;
}

void actor::set_bounds_from_shape(i_collision_shape& shape)
{
	m_bounds.set_from_shape(shape);
	d.unknown1 = m_bounds.get_position() - d.position; // not sure what exactly this is
}

void actor::reset_bounds(float delta)
{
	if (is_moving())
	{
		if (d.rotation_changed)
		{
			set_bounds_from_shape(*m_controller->get_collision());
		}
		else if (d.position_changed)
		{
			m_bounds.set_position(d.position + d.unknown1);
			if (d.size_changed)
			{
				m_bounds.width = d.size.x;
				m_bounds.height = d.size.y;
				m_bounds.max_x = m_bounds.min_x + d.size.x;
				m_bounds.max_y = m_bounds.min_y + d.size.y;
			}
		}
		else if (d.velocity.x != 0.0f || d.velocity.y != 0.0f)
		{
			m_bounds = aabb{ d.position.x, d.position.x + m_bounds.width, d.position.y, d.position.y + m_bounds.height };
		}

		// not sure the purpose of this, but seems to extend the bounds slightly when moving
		if (d.velocity.x < 0.0f)
			m_bounds.min_x += d.velocity.x * delta;
		else
			m_bounds.max_x += d.velocity.x * delta;
		if (d.velocity.y < 0.0f)
			m_bounds.min_y += d.velocity.y * delta;
		else
			m_bounds.max_y += d.velocity.y * delta;
	}
}

void actor::update_collision()
{
	m_state->m_collision_engine.refresh_collisions_on_actor(this);
}

void actor::update_collision(float remaining_delta_time)
{
	m_state->m_collision_engine.refresh_collisions_on_actor(this, remaining_delta_time);
}

void actor::update(timespan time, timespan delta)
{
#ifndef OPTIMIZE_COLLISION
	update_collision();
#endif
	m_controller->update(time, delta);
}

void actor::resolve_collision(timespan time, timespan delta) const
{
	m_controller->resolve_collision(time, delta);
}

void actor::update_position()
{
	for (int32_t i = 0; i < d.total_iterations; i++)
	{
		set_position(d.position + d.velocity * d.iteration_time_step);
	}
	m_bounds.set_position(d.position + d.unknown1);
}