#ifndef TILE_ACTOR_H
#define TILE_ACTOR_H

#include <vector>
#include <memory>

#include "interfaces.h"
#include "aabb.h"
#include "collision_filter.h"
#include "convex_polygon.h"

namespace emu
{
	struct tile_actor : public i_collidable
	{
		collidable_type m_collidable_type = col_air;
		aabb m_bounds;
		shape m_shape_id = shape_aabb;
		aabb m_collision_aabb;
		convex_polygon m_collision_polygon;
		collision_filter m_collision_filter = filter_default;

		tile_actor() = default;
		tile_actor(collidable_type tile_type, i_collision_shape* collision, collision_filter filter);
		tile_actor(collidable_type tile_type, const aabb& bounds, i_collision_shape* collision, collision_filter filter);
		tile_actor(const tile_actor& right) = default;
		~tile_actor() = default;

		tile_actor& operator=(tile_actor&& right) noexcept = default;

		std::unique_ptr<i_clonable> clone() const override;

		void set(collidable_type tile_type, vector position, vector size, i_collision_shape* collision, collision_filter filter);
		void set(const tile_actor& actor);
		vector get_position() const override;
		void set_position(vector position);
		vector get_velocity() const override;
		const aabb& get_bounds() const override;
		i_collision_shape* get_collision() override;
		collision_filter& get_collision_filter() override;
		collidable_type get_collidable_type() const override;
	};

#ifndef OPTIMIZE_COLLISION
	extern const std::vector<std::unique_ptr<tile_actor>> all_tiles;
#endif
}

#endif
