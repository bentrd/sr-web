#ifndef TILE_LAYER_BASE
#define TILE_LAYER_BASE

#include <string>
#include <memory>

#include "aabb.h"
#include "tile_actor.h"

namespace emu
{
	enum tile_id : int32_t
	{
		tile_air = 0,
		tile_square = 1,
		tile_wall_left = 2,
		tile_grapple_ceil = 3,
		tile_wall_right = 4,
		tile_checkered = 5,
		tile_slope_floor_right = 6,
		tile_slope_floor_left = 7,
		tile_stairs_right = 8,
		tile_stairs_left = 9,
		tile_checkered_slope_floor_right = 10,
		tile_checkered_slope_floor_left = 11,
		tile_slope_ceil_right = 12,
		tile_slope_ceil_left = 13,
		tile_checkered_slope_ceil_right = 14,
		tile_checkered_slope_ceil_left = 15,

		tile_count
	};

	struct tile_layer_base
	{
		int32_t m_width = 0;
		int32_t m_height = 0;
		std::unique_ptr<tile_id[]> m_tilemap;

		tile_layer_base();
		tile_layer_base(int32_t width, int32_t height);
		~tile_layer_base() = default;

		tile_layer_base& operator=(tile_layer_base&& right) noexcept = default;

		const tile_id* get_tilemap() const;
		tile_id get_tile(int32_t x, int32_t y) const;
		void set_tile(int32_t x, int32_t y, tile_id tile);
		int32_t get_width() const;
		int32_t get_height() const;
		void get_tile_actors_at(const aabb& aabb_query, collision_filter filter_query) const;
	};
}

#endif
