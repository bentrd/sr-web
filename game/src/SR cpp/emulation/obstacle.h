#ifndef OBSTACLE_H
#define OBSTACLE_H

#include "editable_actor.h"

namespace emu
{
	struct obstacle : public editable_actor
	{
		struct
		{
			float break_rotation_direction{ 0 };
			float break_shade{ 0 };
			float break_timer{ 0 };
			bool is_broken{ 0 };
			bool is_remote_simulated{ 0 };
			timespan break_time{};
			vector break_velocity{ 0, 0 };
		} d;

		obstacle();
		obstacle(const level_actor& def);

		std::unique_ptr<i_clonable> clone() const override;
		bool set(const i_actor_controller* other) override;
		void replace_pointers(const std::map<const i_actor_controller*, i_actor_controller*>& map) override;

		actor_init_params get_actor_params() override;

		collidable_type get_collidable_type() const override;

		void init() override;
		void reset() override;
		void update(timespan time, timespan delta) override;
	
		void break_(timespan time, vector position);
	};
}

#endif