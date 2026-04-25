#ifndef SUPER_BOOST_VOLUME_H
#define SUPER_BOOST_VOLUME_H

#include "resizable_editable_actor.h"

namespace emu
{
	struct super_boost_volume : public resizable_editable_actor
	{
		enum direction
		{
			LEFT = 0,
			LEFT_UP = 1,
			UP = 2,
			RIGHT_UP = 3,
			RIGHT = 4,
			RIGHT_DOWN = 5,
			DOWN = 6,
			LEFT_DOWN = 7
		};

		direction m_direction;

		super_boost_volume();
		super_boost_volume(const level_actor& def);

		std::unique_ptr<i_clonable> clone() const override;
		bool set(const i_actor_controller* other) override;
		void replace_pointers(const std::map<const i_actor_controller*, i_actor_controller*>& map) override;

		actor_init_params get_actor_params() override;

		collidable_type get_collidable_type() const override;

		void init() override;

		vector get_direction_vector() const;
	};
}

#endif