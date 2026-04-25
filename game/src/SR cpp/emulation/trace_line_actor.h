#ifndef TRACE_LINE_ACTOR_H
#define TRACE_LINE_ACTOR_H

#include "interfaces.h"
#include "collision_filter.h"
#include "aabb.h"
#include "line_trace.h"

namespace emu
{
	struct trace_line_actor : i_collidable
	{
		aabb m_bounds;
		line_trace m_collision;
		collision_filter m_collision_filter = filter_all;

		trace_line_actor();
		trace_line_actor(vector start, vector end);
		trace_line_actor(vector start, vector end, collision_filter filter);
		~trace_line_actor() = default;

		trace_line_actor& operator=(trace_line_actor&& right) noexcept = default;

		std::unique_ptr<i_clonable> clone() const override;

		void set(vector start, vector end, collision_filter filter);
		vector get_position() const override;
		vector get_velocity() const override;
		const aabb& get_bounds() const override;
		i_collision_shape* get_collision() override;
		collision_filter& get_collision_filter() override;
		collidable_type get_collidable_type() const override;
	};
}

#endif
