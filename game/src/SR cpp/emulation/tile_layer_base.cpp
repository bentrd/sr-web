#include <algorithm>
#include <cmath>

#include "common.h"
#include "tile_layer_base.h"
#include "caches.h"

using namespace emu;

tile_layer_base::tile_layer_base() :
	m_width{ 0 }, m_height{ 0 }
{

}

tile_layer_base::tile_layer_base(int32_t width, int32_t height) :
	m_width{ width }, m_height{ height }, m_tilemap{ std::make_unique_for_overwrite<tile_id[]>(width * height) }
{
	std::fill(m_tilemap.get(), m_tilemap.get() + width * height, tile_air);
}

const tile_id* tile_layer_base::get_tilemap() const
{
	return m_tilemap.get();
}

tile_id tile_layer_base::get_tile(int32_t x, int32_t y) const
{
	if (m_tilemap == nullptr || x < 0 || x >= m_width || y < 0 || y >= m_height)
		return tile_air;

	return m_tilemap[static_cast<size_t>(x + m_width * y)];
}

void tile_layer_base::set_tile(int32_t x, int32_t y, tile_id tile)
{
	if (m_tilemap == nullptr)
		return;

	if (x >= 0 && x < m_width && y >= 0 && y < m_height)
	{
		m_tilemap[static_cast<size_t>(x + m_width * y)] = tile;
	}
}

int32_t tile_layer_base::get_width() const
{
	return m_width;
}

int32_t tile_layer_base::get_height() const
{
	return m_height;
}

void tile_layer_base::get_tile_actors_at(const aabb& aabb_query, collision_filter filter_query) const
{
#ifndef OPTIMIZE_COLLISION
	if (m_tilemap == nullptr)
		return;
#endif

#ifdef OPTIMIZE_COLLISION
	constexpr float eps = 0.001f;
#else
	constexpr float eps = 0.0f;
#endif

	int32_t begin_x = (int32_t)std::floor((double)(aabb_query.min_x / 16.0f + eps));
	int32_t begin_y = (int32_t)std::floor((double)(aabb_query.min_y / 16.0f + eps));
	int32_t end_x = (int32_t)std::ceil((double)(aabb_query.max_x / 16.0f + eps));
	int32_t end_y = (int32_t)std::ceil((double)(aabb_query.max_y / 16.0f + eps));

#ifndef OPTIMIZE_COLLISION
	if (begin_x == end_x)
	{
		begin_x--;
		end_x++;
	}
	if (begin_y == end_y)
	{
		begin_y--;
		end_y++;
	}
#endif

	begin_x = std::clamp(begin_x, 0, (int32_t)m_width);
	begin_y = std::clamp(begin_y, 0, (int32_t)m_height);
	end_x = std::clamp(end_x, 0, (int32_t)m_width);
	end_y = std::clamp(end_y, 0, (int32_t)m_height);

	auto& tile_actors = caches::inst.tile_actors;
	auto& tile_actors_count = caches::inst.tile_actors_count;
	
	for (int32_t x = begin_x; x < end_x; x++)
	{
		for (int32_t y = begin_y; y < end_y; y++)
		{
			tile_id tile = get_tile(x, y);
			if (tile == tile_air)
				continue;

			collision_filter collision_filter =
				tile != tile_checkered ?
				emu::collision_filter{ 16, 0 } :
				emu::collision_filter{ 64, 0 };

			if (collision_filter.collides_with(filter_query))
			{
				if (tile_actors.size() == tile_actors_count)
					tile_actors.resize(tile_actors_count + 1);

				auto& tile_actor = tile_actors[tile_actors_count++];

#ifdef OPTIMIZE_COLLISION
				if (tile_actor == nullptr)
				{
					tile_actor = std::make_unique<emu::tile_actor>();
					tile_actor->m_collision_polygon.m_vertices = dyn_array<vector>(3);
				}

				vector t{ (float)(x * 16), (float)(y * 16) };

				tile_actor->m_collision_filter = collision_filter;
				tile_actor->m_bounds = aabb{ t.x, t.x + 16.0f, t.y, t.y + 16.0f };
				switch (tile)
				{
				case tile_square:
				case tile_checkered:
					tile_actor->m_collidable_type = col_square;
					tile_actor->m_shape_id = shape_aabb;
					tile_actor->m_collision_aabb = tile_actor->m_bounds;
					break;
				case tile_wall_left:
					tile_actor->m_collidable_type = col_wall_left;
					tile_actor->m_shape_id = shape_aabb;
					tile_actor->m_collision_aabb = tile_actor->m_bounds;
					break;
				case tile_grapple_ceil:
					tile_actor->m_collidable_type = col_grapple_ceil;
					tile_actor->m_shape_id = shape_aabb;
					tile_actor->m_collision_aabb = tile_actor->m_bounds;
					break;
				case tile_wall_right:
					tile_actor->m_collidable_type = col_wall_right;
					tile_actor->m_shape_id = shape_aabb;
					tile_actor->m_collision_aabb = tile_actor->m_bounds;
					break;
				case tile_slope_floor_right:
				case tile_stairs_right:
				case tile_checkered_slope_floor_right:
					tile_actor->m_collidable_type = col_slope_floor_right;
					tile_actor->m_shape_id = shape_convex_polygon;
					tile_actor->m_collision_polygon.m_position = t;
					tile_actor->m_collision_polygon.m_vertices[0] = vector{ 16.0f, 0.0f } + t;
					tile_actor->m_collision_polygon.m_vertices[1] = vector{ 0.0f, 16.0f } + t;
					tile_actor->m_collision_polygon.m_vertices[2] = vector{ 16.0f, 16.0f } + t;
					tile_actor->m_collision_polygon.m_center = vector{ 32.0f / 3, 32.0f / 3 } + t;
					break;
				case tile_slope_floor_left:
				case tile_stairs_left:
				case tile_checkered_slope_floor_left:
					tile_actor->m_collidable_type = col_slope_floor_left;
					tile_actor->m_shape_id = shape_convex_polygon;
					tile_actor->m_collision_polygon.m_position = t;
					tile_actor->m_collision_polygon.m_vertices[0] = vector{ 0.0f, 0.0f } + t;
					tile_actor->m_collision_polygon.m_vertices[1] = vector{ 0.0f, 16.0f } + t;
					tile_actor->m_collision_polygon.m_vertices[2] = vector{ 16.0f, 16.0f } + t;
					tile_actor->m_collision_polygon.m_center = vector{ 16.0f / 3, 32.0f / 3 } + t;
					break;
				case tile_slope_ceil_right:
				case tile_checkered_slope_ceil_right:
					tile_actor->m_collidable_type = col_slope_ceil_right;
					tile_actor->m_shape_id = shape_convex_polygon;
					tile_actor->m_collision_polygon.m_position = t;
					tile_actor->m_collision_polygon.m_vertices[0] = vector{ 0.0f, 0.0f } + t;
					tile_actor->m_collision_polygon.m_vertices[1] = vector{ 16.0f, 16.0f } + t;
					tile_actor->m_collision_polygon.m_vertices[2] = vector{ 16.0f, 0.0f } + t;
					tile_actor->m_collision_polygon.m_center = vector{ 32.0f / 3, 16.0f / 3 } + t;
					break;
				case tile_slope_ceil_left:
				case tile_checkered_slope_ceil_left:
					tile_actor->m_collidable_type = col_slope_ceil_left;
					tile_actor->m_shape_id = shape_convex_polygon;
					tile_actor->m_collision_polygon.m_position = t;
					tile_actor->m_collision_polygon.m_vertices[0] = vector{ 0.0f, 0.0f } + t;
					tile_actor->m_collision_polygon.m_vertices[1] = vector{ 0.0f, 16.0f } + t;
					tile_actor->m_collision_polygon.m_vertices[2] = vector{ 16.0f, 0.0f } + t;
					tile_actor->m_collision_polygon.m_center = vector{ 16.0f / 3, 16.0f / 3 } + t;
					break;
				default: [[unlikely]]
					break;
				}
				tile_actor->m_collision_polygon.m_collidable_type = tile_actor->m_collidable_type;
#else
				const std::unique_ptr<emu::tile_actor>& tile_actor_template = all_tiles[tile];

				if (tile_actor == nullptr)
					tile_actor = ::clone<emu::tile_actor>(tile_actor_template.get());
				else
					tile_actor->set(*tile_actor_template);

				tile_actor->set_position(vector{ (float)(x * 16), (float)(y * 16) });
#endif
			}
		}
	}
}
