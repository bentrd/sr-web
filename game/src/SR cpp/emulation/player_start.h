#ifndef PLAYER_START_H
#define PLAYER_START_H

#include <map>

#include "editable_actor.h"

namespace emu
{
	struct player_start : public editable_actor
	{
		struct
		{
			bool pre_game_lobby{ 0 };
		} d;

		player_start();

		std::unique_ptr<i_clonable> clone() const override;
		bool set(const i_actor_controller* other) override;
		void replace_pointers(const std::map<const i_actor_controller*, i_actor_controller*>& map) override;

		actor_init_params get_actor_params() override;

		collidable_type get_collidable_type() const override;

		void init() override;
	};
}

#endif
