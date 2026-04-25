#ifndef EDITABLE_ACTOR_H
#define EDITABLE_ACTOR_H

#include "actor.h"
#include "collision_filter.h"

namespace emu
{
	struct level_actor;

	struct editable_actor : public i_actor_controller
	{
		struct
		{
			bool unknown1{ 0 };
			bool unknown2{ 0 };
			bool is_editor{ 0 };
			bool unknown4{ 0 };
			bool unknown5{ 0 };
			bool unknown6{ 0 };
			bool unknown7{ 0 };
			bool unknown8{ true };
			bool unknown9{ 0 };
			uint32_t color1{ 0 };
			uint32_t color2{ 0 };
			collision_filter collision_filter{};
			vector unknown3{ 0, 0 };
		} de;

		actor* m_actor;

		aabb m_bounds;

		virtual void replace_pointers(const std::map<const i_actor_controller*, i_actor_controller*>& map) override;

		virtual actor* get_actor() override;
		virtual void set_actor(actor* actor) override;
		virtual i_collision_shape* get_collision() override;
		virtual collision_filter& get_collision_filter() override;
		virtual collidable_type get_collidable_type() const override;

		virtual void resolve_collision(timespan time, timespan delta) override;

		virtual void init() override;
		virtual void reset() override;

		virtual void update(timespan time, timespan delta) override;
	};
}

#endif
