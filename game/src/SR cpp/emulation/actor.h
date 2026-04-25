#ifndef ACTOR_H
#define ACTOR_H

#include <functional>
#include <type_traits>

#include "interfaces.h"
#include "aabb.h"
#include "collision_pair.h"
#include "common.h"

namespace emu
{
	struct collision_engine;
	struct state;

	// actor
	//
	// Represents an entity that can (in most cases) interact with other actors and the level and
	// is stored in the collision engine.
	// The actor class contains a lot of information that is mostly relevant to the collision engine
	// and a lot of basic information like position and velocity.
	// Each actor contains an actor controller, which is the 'i_actor_controller' interface and
	// a different implementation for each type of actor. The actor controller stores all the
	// data and handles all the logic relevant to the specific actor type.
	struct actor : public i_collidable, public i_controllable
	{
		struct
		{
			int32_t id{ 0 }; // actor id, where each individual actor gets assigned a unique id when spawning
			float rotation{ 0 }; // rotation
			float collision_sweep_minimal_density{ 1.0f }; // used by the collision engine to determine 'm_total_iterations'
			int32_t total_iterations{ 0 }; // amount of iterations per collision resolve
			float iteration_time_step{ 0 }; // time step per iteration
			bool position_changed{ 0 }; // true if position has changed
			bool rotation_changed{ 0 }; // true if rotation has changed
			bool size_changed{ 0 }; // true if size has changed
			bool automatic_collision_detection{ 0 }; // if true, the collision engine will automatically detect all collisions before 'resolve_collision' gets called
			bool is_collision_active{ true }; // if false, the collision engine will not automatically detect collisions
			bool is_collidable{ 0 }; // if true, the entity has collision and will be added to the collision engine's quad tree
			bool should_predict_collision{ 0 }; // if false, the collision sweep minimal density gets ignored and the actor will always have 1 total iteration
			bool unknown2{ 0 }; // unused for now
			bool local_player{ 0 }; // placed by Velo
			bool ghost_owned_item{ 0 }; // placed by Velo
			vector position{ 0, 0 }; // position
			vector size{ 0, 0 }; // size
			vector unknown1{ 0, 0 }; // not quite sure, but influences the 'm_bounds' position slightly
			vector velocity{ 0, 0 }; // velocity
		} d;

		std::unique_ptr<i_actor_controller> m_controller; // actor controller
		state* m_state; // reference to the state
		aabb m_bounds; // bounding box
		
		quad_tree_node* m_quad_tree_parent = nullptr; // quad tree parent node
	
		std::vector<collision_pair> m_collisions; // list of all collisions

#ifdef OPTIMIZE_COLLISION
		bool m_has_update = true;
		bool m_is_movable = true;
#endif

		actor(state* state, int32_t id, vector position, vector size);
		actor(const actor& right);
		~actor() = default;

		actor& operator=(const actor& right);

		std::unique_ptr<i_clonable> clone() const override;

		void set_controller(std::unique_ptr<i_actor_controller>&& controller);

		int32_t get_id() const;
		i_actor_controller* get_controller() override;
		vector get_position() const override;
		void set_position(vector pos);
		float get_rotation() const;
		void set_rotation(float rotation);
		vector get_size() const;
		void set_size(vector size);
		const aabb& get_bounds() const override;
		vector get_velocity() const override;
		void set_velocity(vector vel);
		bool is_moving() const;
		quad_tree_node* get_quad_tree_parent() const;
		void set_quad_tree_parent(quad_tree_node* parent);
		i_collision_shape* get_collision() override;
		collision_filter& get_collision_filter() override;
		collidable_type get_collidable_type() const override;
		collision_pair& add_collision(i_collidable* source, i_collidable* target, vector source_pos, vector source_vel, vector target_pos, vector target_vel);
		collision_pair& add_collision(collision_pair& other);
		int32_t get_collision_count() const;
		collision_pair* get_collision(int32_t index);
		void reset_collision(float delta, bool unused);
		void reset_changed();
		void set_bounds_from_shape(i_collision_shape& shape);
		void reset_bounds(float delta);
		void update_collision();
		void update_collision(float remaining_delta_time);
		void update(timespan time, timespan delta);
		void resolve_collision(timespan time, timespan delta) const;
		void update_position(); // added this function myself to update position when updating with 'ignore_collision'
	};
}

#endif