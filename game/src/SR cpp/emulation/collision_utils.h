#ifndef COLLISION_UTILS_H
#define COLLISION_UTILS_H

#include <functional>

#include "interfaces.h"

namespace emu
{
	struct collision_utils
	{
		static bool intersect(i_collision_shape* source, i_collision_shape* target, vector& mtd);
		static bool intersect(vector p0, vector p1, vector q0, vector q1, vector& intersect_point);
		static float cross(vector v1, vector v2);
		static bool aabb_aabb_intersect(aabb* source, aabb* target, vector& mtd);
		static bool polygon_polygon_intersect(std::derived_from<i_collision_shape> auto* source, std::derived_from<i_collision_shape> auto* target, vector& mtd);
		static void project_to_axis(vector axis, std::derived_from<i_collision_shape> auto* shape, float& start, float& end);
		static bool do_projections_overlap(float start1, float end1, float start2, float end2, float& overlap_length);
		static void update_mtd(vector axis, float overlap_length, vector& mtd, float& min);

		static void assign_collision_shape(std::unique_ptr<i_collision_shape>& left, const i_collision_shape* right);
	};
}

#include "collision_utils_templates.cpp"

#endif
