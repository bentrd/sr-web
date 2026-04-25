#include "grapple.h"
#include "common.h"
#include "tile_actor.h"
#include "player.h"
#include "physics_constants.h"

using namespace emu;

std::unique_ptr<i_clonable> grapple::clone() const
{
	return std::make_unique<grapple>(*this);
}

bool grapple::set(const i_actor_controller* other)
{
	if (const grapple* ptr_cast = dynamic_cast<const grapple*>(other))
	{
		*this = *ptr_cast;
		return true;
	}
	return false;
}

void grapple::replace_pointers(const std::map<const i_actor_controller*, i_actor_controller*>& map)
{
	auto it = map.find(m_owner);
	if (it != map.end())
		m_owner = dynamic_cast<player*>(it->second);
}

actor_init_params grapple::get_actor_params()
{
	return
	{
		.size = vector{ 12.0f, 12.0f },
		.auto_col_det = true,
		.is_col = true,
		.should_pred_col = true,
#ifdef OPTIMIZE_COLLISION
		.has_update = false,
		.is_movable = true
#endif
	};
}

actor* grapple::get_actor()
{
	return m_actor;
}

void grapple::set_actor(actor* actor)
{
	m_actor = actor;
}

i_collision_shape* grapple::get_collision()
{
	m_bounds = aabb(m_actor->d.position.x, m_actor->d.position.x + m_actor->d.size.x, m_actor->d.position.y, m_actor->d.position.y + m_actor->d.size.y);
	return &m_bounds;
}

collision_filter& grapple::get_collision_filter()
{
	return d.collision_filter;
}

collidable_type grapple::get_collidable_type() const
{
	return col_hook;
}

vector grapple::get_center() const
{
	return m_actor->d.position + m_actor->d.size / 2.0f;
}

void grapple::init()
{
	m_actor->d.is_collision_active = false;
}

void grapple::reset()
{

}

void grapple::connect(vector position)
{
	m_actor->d.is_collision_active = true;
	d.is_connected = true;
	// ignore graphics code
	m_actor->d.velocity = vec_zero;
	m_actor->set_position(position);
	// ignore sound code
}

void grapple::shoot(vector position, vector direction)
{
	m_actor->d.is_collision_active = true;
	d.is_connected = false;
	m_actor->set_position(position);
	m_actor->d.velocity = direction * physics::grapple_hook_speed;
	d.direction = direction;
	// ignore graphics code
	d.collision_filter.m_category_bits = 0x01000000u;
}

void grapple::cancel()
{
	m_actor->d.is_collision_active = false;
	d.is_connected = false;
	// ignore graphics code
	d.collision_filter.m_category_bits = 0x0u;
	m_actor->d.velocity = vec_zero;
}

void grapple::resolve_collision(timespan time, timespan delta)
{
	if (!m_actor->d.is_collision_active || d.is_connected)
		return;
	
	float delta_s = (float)delta.seconds();
	vector center = get_center();
	i_collidable* closest_tile_actor = nullptr;
	collision_pair* closest_collision = nullptr;
	float min_distance = FLOAT_MAX;
	bool collision_found = false;
	for (int32_t i = 0; i < m_actor->get_collision_count(); i++)
	{
		collision_pair* collision = m_actor->get_collision(i);
		if (collision->m_is_colliding)
		{
			collision_found = true;
			if (closest_collision == nullptr)
				closest_collision = collision;
			if (tile_actor* tile_actor = dynamic_cast<::tile_actor*>(collision->m_target))
			{
				float distance = (center - tile_actor->get_bounds().get_center()).length_sqr();
				if (distance < min_distance || (distance == min_distance && tile_actor->get_collidable_type() == col_grapple_ceil))
				{
					closest_collision = collision;
					closest_tile_actor = collision->m_target;
					min_distance = distance;
				}
			}
		}
	}
	if (collision_found)
	{
		if (closest_tile_actor != nullptr && closest_tile_actor->get_collidable_type() == col_grapple_ceil)
		{
			connect(vector{
					std::min(closest_tile_actor->get_bounds().max_x - 1.5f * m_bounds.width, std::max(closest_tile_actor->get_bounds().min_x - 0.5f * m_bounds.width, m_actor->d.position.x)),
					closest_tile_actor->get_bounds().max_y
				});
			// ignore sound code
		}
		else
		{
			if (closest_collision != nullptr)
				m_actor->d.position = closest_collision->m_source_pos + closest_collision->m_mtd;
			// ignore graphics code
			cancel();
			// ignore sound code
		}
	}
	else
		m_actor->set_position(m_actor->d.position + m_actor->d.velocity * delta_s);

	// ignore graphics and sound code
}

void grapple::update(timespan time, timespan delta)
{
	// ignore graphics code
}
