#ifndef PLAYER_H
#define PLAYER_H

#include <memory>

#include "vector.h"
#include "grapple.h"
#include "interfaces.h"
#include "aabb.h"
#include "actor.h"
#include "collision_filter.h"
#include "collision_engine.h"
#include "encrypted_float.h"

namespace emu
{
	struct player : public i_actor_controller
	{
		struct
		{
			float delta{ 0 };
			int32_t player_game_mode{ 6 };
			int32_t unknown28{ 0 };
			uint32_t col_filter1_mask{ 0x00331070u };
			float unused{ 0 }; // completely unused
			int32_t dominating_direction{ 0 };
			float base_acceleration{ 250.0f };
			int32_t move_direction{ 1 };
			float jump_strength{ 2876.0f };
			int32_t jump_count{ 2 };
			int32_t prev_jump_state{ 1 };
			int32_t jump_state{ 0 };
			int32_t next_jump_state{ 0 };
			int32_t hit_by_explosion_id{ INT32_MIN }; // actor ID
			int32_t stunned_by_laser_id{ INT32_MIN }; // actor ID
			float dive_timer{ 0 }; // has only visual effects
			collidable_type ground_collidable_type{ col_air }; // collision ID of the ground the player is standing on
			int32_t stunned_by_id{ INT32_MIN }; // actor ID
			float jump_duration{ 0.25f };
			int32_t killed_by_id{ 0 }; // actor ID
			float swing_radius{ 0 };
			float swing_angle{ 0 };
			float unknown29{ 10.0f }; // constant
			int32_t place{ 0 };
			int32_t player_count{ 0 };
			float win_star_rotation_speed{ 0 }; // has only visual effects
			float boost{ 0 };
			int32_t collected_sandals{ 0 };
			float boost_cooldown{ 0 };
			float shockwave_charge{ 0 };
			float wall_jump_bonus_timer{ 0 };
			float unknown6{ 0 };
			float progress{ 0 };
			float travel_distance{ 0 };
			float timer{ 0 };
			float airtime{ 0 };
			float item_cooldown{ 0 }; // cooldown for picking up items
			float bounce_pad_timer{ 0 };
			float stun_timer{ 0 };
			float wrong_way_timer{ 0 };
			float frozen_timer{ 0 };
			float travel_distance_on_becoming_airborne{ 0 };
			float super_speed{ 1200.0f };
			float swap_item_cooldown{ 0 };
			float unknown32{ 0 };
			float unknown23{ 0 };
			bool is_destroyed{ 0 };
			int8_t character_id{ 0 };
			int8_t skin_id{ 0 };
			bool left_held{ 0 };
			bool right_held{ 0 };
			bool jump_held{ 0 };
			bool grapple_held{ 0 };
			bool item_pressed{ 0 }; // instantly set to false after pressing
			bool item_held{ 0 };
			bool taunt_held{ 0 };
			bool swap_item_held{ 0 };
			bool was_item_held{ 0 };
			bool slide_held{ 0 };
			bool boost_held{ 0 };
			bool is_hit_by_explosion{ 0 };
			bool explosion_hit_is_rocket{ 0 };
			bool is_stunned_by_laser{ 0 };
			bool is_in_air{ 0 };
			bool can_repress_jump{ true }; // jump when jump button is pressed
			bool can_buffer_wall_jump{ 0 };
			bool is_sliding{ 0 };
			bool was_slide_stopped{ 0 };
			bool is_foley_slide{ 0 };
			bool was_slide_cancelled{ 0 };
			bool is_stumbling{ 0 };
			bool is_tackled{ 0 };
			bool can_grapple{ true };
			bool is_grappling{ 0 };
			bool is_shooting{ 0 };
			bool is_swinging{ 0 };
			bool is_using_boost{ 0 };
			bool is_inside_super_boost_wind{ 0 };
			bool is_climbing{ 0 };
			bool is_on_wall{ 0 };
			bool is_climbing_up{ 0 };
			bool unused2{ 0 };
			bool is_on_ground{ 0 };
			bool is_on_fall_tile{ 0 };
			bool is_ceiling_hit{ 0 };
			bool is_colliding_with_solid{ 0 };
			bool is_stunned{ 0 };
			bool is_hooked{ 0 };
			bool is_hooked2{ 0 };
			bool is_using_drill{ 0 };
			bool is_fly_mode{ 0 }; // ignores collision and speedhack
			bool has_touched_finish_bomb{ 0 };
			bool can_climb_up{ 0 };
			bool unused3{ 0 };
			bool is_sudden_death{ 0 };
			bool is_dying{ 0 };
			bool is_dying2{ 0 };
			bool is_winning{ 0 };
			bool is_taunting{ 0 };
			bool can_untaunt{ 0 };
			bool was_network_position_updated{ 0 };
			bool win_star_expanding{ 0 };
			int8_t item_id{ 0 };
			bool enable_grapples{ 0 };
			bool enable_climbing{ 0 };
			bool enable_double_jump{ 0 };
			bool is_inside_super_boost{ 0 };
			bool is_editing{ 0 };
			bool is_inside_boost_section{ 0 };
			bool was_inside_boost_section{ 0 };
			bool is_wrong_way{ 0 };
			bool is_frozen{ 0 };
			bool item_has_hit{ 0 };
			bool is_still_alive{ 0 };
			bool enable_inputs{ true };
			bool is_swap_item_cooldown_active{ 0 };
			bool unknown38{ 0 };
			bool unknown22{ 0 };
			vector position_ground{ 0, 0 }; // only updates when on ground and not sliding
			collision_filter collision_filter1{ 0x10000000u, 0x00331070u };
			collision_filter collision_filter2{ 0x10000000u, 0xEEFFFEBFu };
			collision_filter unused4{ 0x0u, 0x10u };
			vector previous_position{ 0, 0 };
			vector displacement{ 0, 0 };
			vector ground_normal{ 0, 0 };
			vector collision_tangent{ 0, 0 };
			timespan jump_time{ 0ull };
			vector jump_direction{ vec_up };
			vector jump_velocity{ 0, 0 };
			vector initial_jump_velocity{ 0, 0 };
			timespan repress_jump_time{ 0ull };
			timespan wall_get_off_time{ 0ull };
			vector solid_collision_mtd{ 0, 0 };
			vector explosion_knockback{ 0, 0 };
			timespan stunned_time{ 0ull };
			vector network_target_position{ 0, 0 };
			vector unknown41{ 0, 0 };
			vector super_boost_direction{ 0, 0 };
			vector super_boost_force{ 0, 0 };
			timespan stumble_time{ 0ull };
			timespan grapple_time{ 0ull };
			timespan slide_time{ 0ull };
			timespan slide_cancel_time{ 0ull };
			timespan drill_time{ 0ull };
			timespan round_start_time{ 0ull };
			timespan hooked_time{ 0ull };
			timespan unknown43{ 10000000ull };
			timespan super_speed_duration{ 0ull };
			timespan item_has_hit_time{ 0ull };
			timespan still_alive_time{ 0ull };
			timespan unknown21{ 2500ull * 1000ull * 10000ull };
			timespan still_alive_popup_delay{ 1500ull * 1000ull * 10000ull };
			encrypted_float boostacoke{ 0.0f };
		} d;

		actor* m_actor;

		aabb m_standing_hitbox;
		aabb m_sliding_hitbox;

		i_collidable* m_unknown13;

		grapple* m_grapple = nullptr;

		int32_t m_player_index = 0;

		std::unique_ptr<i_clonable> clone() const override;
		bool set(const i_actor_controller* other) override;
		void replace_pointers(const std::map<const i_actor_controller*, i_actor_controller*>& map) override;

		void set_position(vector position);

		actor_init_params get_actor_params() override;
		actor* get_actor() override;
		void set_actor(actor* actor) override;
		i_collision_shape* get_collision() override;
		collision_filter& get_collision_filter() override;
		collidable_type get_collidable_type() const override;

		void init() override;

		void init_hitboxes();

		void reset() override;

		float get_velocity_multiplier();
		float get_acceleration_multiplier();

		void update_hitboxes();
		void update_basic(timespan time, timespan delta);
		void update_base_acceleration(vector velocity);
		void get_grapple_position_and_direction(vector& position, vector& direction);
		void shoot_grapple(timespan time, vector position, vector direction);
		void connect_grapple(vector grapple_position, float radius, int direction);
		void update_jump(timespan time, timespan delta, vector& velocity, timespan jump_time);
		void reset_jump();
		void cancel_grapple();
		void get_off_wall(timespan time);
		void unfreeze();
		void hit_obstacle(timespan time, vector velocity);

		bool is_solid(i_collidable* a1) const;
		void unknown3(timespan time, i_collidable* a1);
		void unknown4(timespan time, i_collidable* a1);
		void update_ground_normal();
		void unknown6();
		void unknown11(timespan time, i_collidable* a1);
		void check_collision(timespan time, timespan delta);

		void resolve_object_collisions(int max_iteration);
		void resolve_object_collision(i_collidable* a1);
		void resolve_collision_implementation(timespan time);
		bool any_collision_with_solid(int32_t a1, vector& a2);
		bool unknown9(int32_t a1, int32_t a2);
		bool can_climb(i_collidable* a1);
		void attach_wall(timespan time, int32_t a1, i_collidable* a2);
		void resolve_collision(timespan time, timespan delta) override;

		void unknown12();

		void update_item(timespan time);

		void update(timespan time, timespan delta) override;
	};
}

#endif