#ifndef TRACE_POLYGON_ACTOR_H
#define TRACE_POLYGON_ACTOR_H

#include "interfaces.h"
#include "aabb.h"
#include "collision_filter.h"

namespace emu
{
	struct trace_polygon_actor : public i_collidable
	{
		aabb m_bounds;
		std::unique_ptr<i_collision_shape> m_collision = nullptr;
		collision_filter m_collision_filter = filter_all;

		trace_polygon_actor();
		trace_polygon_actor(const aabb& bounds, i_collision_shape* collision);
		trace_polygon_actor(const aabb& bounds, i_collision_shape* collision, collision_filter filter);
		~trace_polygon_actor() = default;

		trace_polygon_actor& operator=(trace_polygon_actor&& right) noexcept = default;

		std::unique_ptr<i_clonable> clone() const override;

		void set(const aabb& bounds, i_collision_shape* collision);
		void set(const aabb& bounds, i_collision_shape* collision, collision_filter filter);
		vector get_position() const override;
		vector get_velocity() const override;
		const aabb& get_bounds() const override;
		i_collision_shape* get_collision() override;
		collision_filter& get_collision_filter() override;
		collidable_type get_collidable_type() const override;
	};
}

#endif
