#ifndef BOOST_SECTION_H
#define BOOST_SECTION_H

#include "editable_actor.h"
#include "convex_polygon.h"

namespace emu
{
	struct boost_section : public editable_actor
	{
		enum rotation
		{
			DEGREE_0,
			DEGREE_45,
			DEGREE_90,
			DEGREE_135,
			DEGREE_180,
			DEGREE_MINUS_135,
			DEGREE_MINUS_90,
			DEGREE_MINUS_45
		};

		rotation m_rotation;

#ifdef OPTIMIZE_COLLISION
		union
		{
			aabb m_aabb;
			convex_polygon m_polygon;
		};
#else
		convex_polygon m_polygon;
#endif

		boost_section();
		boost_section(const level_actor& def);
#ifdef OPTIMIZE_COLLISION
		boost_section(const boost_section& right);
		boost_section(boost_section&&) noexcept = delete;
		~boost_section();

		boost_section& operator=(const boost_section& right);
		boost_section& operator=(boost_section&&) noexcept = delete;
#endif

		std::unique_ptr<i_clonable> clone() const override;
		bool set(const i_actor_controller* other) override;
		void replace_pointers(const std::map<const i_actor_controller*, i_actor_controller*>& map) override;

		actor_init_params get_actor_params() override;

		i_collision_shape* get_collision() override;
		collidable_type get_collidable_type() const override;

		void init() override;
	};
}

#endif
