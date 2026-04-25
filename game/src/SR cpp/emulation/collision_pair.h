#ifndef COLLISION_PAIR_H
#define COLLISION_PAIR_H

#include "interfaces.h"

namespace emu
{
	// collision pair
	//
	// Represents a collision between two actors and stores some useful information about it.
	struct collision_pair
	{
		i_collidable* m_source;
		i_collidable* m_target;
		vector m_source_pos;
		vector m_source_vel;
		vector m_target_pos;
		vector m_target_vel;
		bool m_is_colliding;
		int32_t m_iteration;
		vector m_mtd;

		collision_pair();
		collision_pair(i_collidable* source, i_collidable* target, vector source_pos, vector source_vel, vector target_pos, vector target_vel);

		void find_blocking_axis_on_target(vector& vertex0, vector& vertex1);
	};
}

#endif
