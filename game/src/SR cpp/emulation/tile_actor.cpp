#include "tile_actor.h"
#include "convex_polygon.h"
#include "collidable_type.h"
#include "line_trace.h"
#include "collision_utils.h"

using namespace emu;

tile_actor::tile_actor(collidable_type tile_type, i_collision_shape* collision, collision_filter filter)
{
	m_collidable_type = tile_type;
	m_bounds.set_from_shape(*collision);
	if (collision != nullptr)
	{
		m_shape_id = collision->get_id();
		if (m_shape_id == shape_aabb)
			m_collision_aabb = *static_cast<aabb*>(collision);
		else
			m_collision_polygon = *static_cast<convex_polygon*>(collision);
	}

	m_collision_filter = filter;
}

tile_actor::tile_actor(collidable_type tile_type, const aabb& bounds, i_collision_shape* collision, collision_filter filter)
{
	m_collidable_type = tile_type;
	m_bounds = bounds;
	if (collision != nullptr)
	{
		m_shape_id = collision->get_id();
		if (m_shape_id == shape_aabb)
			m_collision_aabb = *static_cast<aabb*>(collision);
		else
			m_collision_polygon = *static_cast<convex_polygon*>(collision);
	}

	m_collision_filter = filter;
}

std::unique_ptr<i_clonable> tile_actor::clone() const
{
	return std::make_unique<tile_actor>(*this);
}

void tile_actor::set(collidable_type tile_type, vector position, vector size, i_collision_shape* collision, collision_filter filter)
{
	m_collidable_type = tile_type;
	m_bounds = aabb{ position.x, position.x + size.x, position.y, position.y + size.y };
	m_shape_id = collision->get_id();
	if (m_shape_id == shape_aabb)
		m_collision_aabb = *static_cast<aabb*>(collision);
	else
		m_collision_polygon = *static_cast<convex_polygon*>(collision);
	m_collision_filter = filter;
}

void tile_actor::set(const tile_actor& actor)
{
	m_collidable_type = actor.m_collidable_type;
	m_bounds = actor.m_bounds;
	m_shape_id = actor.m_shape_id;
	if (m_shape_id == shape_aabb)
		m_collision_aabb = actor.m_collision_aabb;
	else
		m_collision_polygon = actor.m_collision_polygon;
	m_collision_filter = actor.m_collision_filter;
}

vector tile_actor::get_position() const
{
	return m_bounds.get_position();
}

void tile_actor::set_position(vector position)
{
	m_bounds.set_position(position);
	if (m_shape_id == shape_aabb)
		m_collision_aabb.set_position(position);
	else
		m_collision_polygon.set_position(position);
}

vector tile_actor::get_velocity() const
{
	return vector{ 0.0f, 0.0f };
}

const aabb& tile_actor::get_bounds() const
{
	return m_bounds;
}

i_collision_shape* tile_actor::get_collision()
{
	if (m_shape_id == shape_aabb)
		return &m_collision_aabb;
	else
		return &m_collision_polygon;
}

collision_filter& tile_actor::get_collision_filter()
{
	return m_collision_filter;
}

collidable_type tile_actor::get_collidable_type() const
{
	return m_collidable_type;
}

#ifndef OPTIMIZE_COLLISION
const std::vector<std::unique_ptr<tile_actor>> emu::all_tiles = []()
	{
		collision_filter default_filter = collision_filter{ 16, 0 };
		convex_polygon slope_ceil_right = convex_polygon({
				vector{ 0.0f, 0.0f },
				vector{ 16.0f, 16.0f },
				vector{ 16.0f, 0.0f }
			});
		convex_polygon slope_ceil_left = convex_polygon({
				vector{ 0.0f, 0.0f },
				vector{ 0.0f, 16.0f },
				vector{ 16.0f, 0.0f }
			});
		convex_polygon slope_floor_right = convex_polygon({
				vector{ 16.0f, 0.0f },
				vector{ 0.0f, 16.0f },
				vector{ 16.0f, 16.0f }
			});
		convex_polygon slope_floor_left = convex_polygon({
				vector{ 0.0f, 0.0f },
				vector{ 0.0f, 16.0f },
				vector{ 16.0f, 16.0f }
			});
		aabb square = ::aabb{ 0.0f, 16.0f, 0.0f, 16.0f };

		std::vector<std::unique_ptr<tile_actor>> all_tiles;

		all_tiles.emplace_back(nullptr); // air

		all_tiles.emplace_back(std::make_unique<tile_actor>(col_square, &square, default_filter));
		all_tiles.emplace_back(std::make_unique<tile_actor>(col_wall_left, &square, default_filter));
		all_tiles.emplace_back(std::make_unique<tile_actor>(col_grapple_ceil, &square, default_filter));
		all_tiles.emplace_back(std::make_unique<tile_actor>(col_wall_right, &square, default_filter));
		all_tiles.emplace_back(std::make_unique<tile_actor>(col_square, &square, collision_filter{ 64, 0 }));
		all_tiles.emplace_back(std::make_unique<tile_actor>(col_slope_floor_right, &slope_floor_right, default_filter));
		all_tiles.emplace_back(std::make_unique<tile_actor>(col_slope_floor_left, &slope_floor_left, default_filter));
		all_tiles.emplace_back(std::make_unique<tile_actor>(col_slope_floor_right, &slope_floor_right, default_filter));
		all_tiles.emplace_back(std::make_unique<tile_actor>(col_slope_floor_left, &slope_floor_left, default_filter));
		all_tiles.emplace_back(std::make_unique<tile_actor>(col_slope_floor_right, &slope_floor_right, default_filter));
		all_tiles.emplace_back(std::make_unique<tile_actor>(col_slope_floor_left, &slope_floor_left, default_filter));
		all_tiles.emplace_back(std::make_unique<tile_actor>(col_slope_ceil_right, &slope_ceil_right, default_filter));
		all_tiles.emplace_back(std::make_unique<tile_actor>(col_slope_ceil_left, &slope_ceil_left, default_filter));
		all_tiles.emplace_back(std::make_unique<tile_actor>(col_slope_ceil_right, &slope_ceil_right, default_filter));
		all_tiles.emplace_back(std::make_unique<tile_actor>(col_slope_ceil_left, &slope_ceil_left, default_filter));

		return all_tiles;
	}();
#endif
