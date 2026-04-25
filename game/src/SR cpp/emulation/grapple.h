#ifndef GRAPPLE_H
#define GRAPPLE_H

#include <cstdint>

#include "vector.h"
#include "interfaces.h"
#include "actor.h"
#include "collision_filter.h"

namespace emu
{
	struct player;

	struct grapple : public i_actor_controller
	{
		struct
		{
			bool is_connected{ 0 };
			collision_filter collision_filter{ 0x0u, 0x0EF0CFBCu };
			vector direction{ 1.0f, 1.0f };
		} d;

		actor* m_actor;
		player* m_owner;

		aabb m_bounds;

		std::unique_ptr<i_clonable> clone() const override;
		bool set(const i_actor_controller* other) override;
		void replace_pointers(const std::map<const i_actor_controller*, i_actor_controller*>& map) override;

		actor_init_params get_actor_params() override;
		actor* get_actor() override;
		void set_actor(actor* actor) override;
		i_collision_shape* get_collision() override;
		collision_filter& get_collision_filter() override;
		collidable_type get_collidable_type() const override;
		vector get_center() const;

		void init() override;
		void reset() override;

		void connect(vector position);
		void shoot(vector position, vector direction);
		void cancel();

		void resolve_collision(timespan time, timespan delta) override;

		void update(timespan time, timespan delta) override;
	};
}

#endif
