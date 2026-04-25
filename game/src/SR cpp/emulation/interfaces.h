#ifndef INTERFACES_H
#define INTERFACES_H

#include <cstdint>
#include <vector>
#include <map>

#include "common.h"
#include "vector.h"
#include "timespan.h"
#include "collidable_type.h"

namespace emu
{
	struct aabb;
	struct collision_filter;
	struct quad_tree_node;
	struct actor;
	struct tile_actor;

	enum shape
	{
		shape_aabb,
		shape_convex_polygon,
		shape_line_trace
	};

	struct i_collision_shape : public i_clonable
	{
		virtual ~i_collision_shape() = default;

		virtual shape get_id() const = 0;
		virtual vector get_position() const = 0;
		virtual void set_position(vector pos) = 0;
		virtual vector get_center() const = 0;
		virtual vector get_pivot() const = 0;
		virtual void set_pivot(vector pivot) = 0;
		virtual float get_rotation() const = 0;
		virtual void set_rotation(float rotation) = 0;
		virtual int32_t get_vertex_count() const = 0;
		virtual vector get_vertex(int32_t index) const = 0;
		virtual void get_vertices(std::vector<vector>& vertices) const = 0;
	};

	struct i_collidable : public i_clonable
	{
		virtual ~i_collidable() = default;
		
		virtual vector get_position() const = 0;
		virtual vector get_velocity() const = 0;
		virtual const aabb& get_bounds() const = 0;
		virtual i_collision_shape* get_collision() = 0;
		virtual collision_filter& get_collision_filter() = 0;
		virtual collidable_type get_collidable_type() const = 0;
	};

	struct actor_init_params
	{
		vector size;
		bool auto_col_det;
		bool is_col;
		bool should_pred_col;
#ifdef OPTIMIZE_COLLISION
		bool has_update = true;
		bool is_movable = true;
#endif
	};

	struct i_actor_controller : public i_clonable
	{
		virtual ~i_actor_controller() = default;

		virtual bool set(const i_actor_controller* other) = 0;
		virtual void replace_pointers(const std::map<const i_actor_controller*, i_actor_controller*>& map) = 0;

		virtual actor_init_params get_actor_params() = 0;
		virtual actor* get_actor() = 0;
		virtual void set_actor(actor* actor) = 0;
		virtual i_collision_shape* get_collision() = 0;
		virtual collision_filter& get_collision_filter() = 0;
		virtual collidable_type get_collidable_type() const = 0;

		virtual void resolve_collision(timespan time, timespan delta) = 0;
		
		virtual void init() = 0;
		virtual void reset() = 0;
		virtual void update(timespan time, timespan delta) = 0;
	};

	struct i_controllable
	{
		virtual ~i_controllable() = default;

		virtual i_actor_controller* get_controller() = 0;
	};

	struct i_actor_def
	{
		virtual ~i_actor_def() = default;

		virtual vector get_pos() const = 0;
		virtual void set_pos(vector pos) = 0;
		virtual vector get_size() const = 0;
		virtual int32_t get_actor_id() const = 0;
		virtual bool get_automatic_collision_detection() const = 0;
		virtual bool get_is_collidable() const = 0;
		virtual bool get_should_predict_collision() const = 0;

		virtual i_actor_controller* create_controller() = 0;
	};
}

#endif
