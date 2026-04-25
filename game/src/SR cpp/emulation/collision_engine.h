#ifndef COLLISION_ENGINE_H
#define COLLISION_ENGINE_H

#include <vector>
#include <functional>

#include "interfaces.h"
#include "quad_tree_node.h"
#include "tile_actor.h"
#include "collision_pair.h"
#include "actor.h"
#include "trace_line_actor.h"
#include "trace_polygon_actor.h"
#include "tile_layer_base.h"
#include "level.h"

namespace emu
{
	// collision engine
	//
	// Stores all the actors and provides lots of utilities for handling collision.
	struct collision_engine
	{
		quad_tree_node m_quad_tree = quad_tree_node{ aabb{} };
		std::vector<std::unique_ptr<actor>> m_actors; // this list owns all the actors
		std::vector<actor*> m_auto_col_det_actors; // all actors with 'm_automatic_collision_detection' set to true
#ifdef OPTIMIZE_COLLISION
		std::vector<actor*> m_actors_has_update;
		std::vector<actor*> m_actors_is_movable;
#endif
		level* m_level;

		collision_engine();
		collision_engine(const collision_engine& right);
		collision_engine(collision_engine&&) noexcept = default;

		collision_engine& operator=(const collision_engine& right);
		collision_engine& operator=(collision_engine&&) noexcept = default;

		void clear();
		void reset_actors();
		void add_actor(std::unique_ptr<actor>&& actor);
		void remove_actor(actor* actor);
		void swap_actors(actor* actor1, actor* actor2);

		int32_t get_actor_count() const;
		actor* get_actor(int32_t index);
		actor* get_actor_by_id(int32_t id);
		void add_collision_pair(
			i_collidable* source, i_collidable* target,
			vector source_pos, vector source_vel,
			vector target_pos, vector target_vel);
		void set_world_bounds(const aabb& a1); // unused for now
		int32_t trace(vector start_point, vector end_point, std::vector<collision_pair*>& collisions);
		int32_t trace(vector start_point, vector end_point, collision_filter filter, std::vector<collision_pair*>& collisions);
		collision_pair* trace_single(vector start_point, vector end_point);
		collision_pair* trace_single(vector start_point, vector end_point, collision_filter filter);
		collision_pair* trace_single(vector start_point, vector end_point, collision_filter filter, float segment_length);
		int32_t check_shape(const aabb& bounds, i_collision_shape& collision, std::vector<collision_pair*>& collisions);
		int32_t check_shape(const aabb& bounds, i_collision_shape& collision, collision_filter filter, std::vector<collision_pair*>& collisions);
		void refresh_collisions_on_actor(actor* actor);
		void refresh_collisions_on_actor(actor* actor, float remaining_delta_time);
		int32_t get_collisions_on_actor(actor* actor, std::vector<collision_pair*>& collisions);
		void clear_collisions();
		void update_collisions(float delta_s);
		void get_collision_candidates_all_actors();
		void get_collision_candidates(std::derived_from<i_collidable> auto* collidable); // stores collision pairs in 'm_collision_pairs_buf'
		void get_collision_candidates(
			std::derived_from<i_collidable> auto* collidable,
			std::invocable<i_collidable*, i_collidable*, vector, vector, vector, vector> auto&& add_collision);
		void check_collision_candidates_all_actors(float delta_s);
		void check_collision_candidates_actor(actor* actor, float delta_s);
		int32_t check_collision_candidates_from_buf(std::vector<collision_pair*>& colliding);
	};
}

#include "collision_engine_templates.cpp"

#endif
