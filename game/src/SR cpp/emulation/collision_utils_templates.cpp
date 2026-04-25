#ifdef COLLISION_UTILS_H

#include "config.h"
#include "line_trace.h"
#include "aabb.h"
#include "convex_polygon.h"

namespace emu
{
	bool collision_utils::polygon_polygon_intersect(
		std::derived_from<i_collision_shape> auto* source, 
		std::derived_from<i_collision_shape> auto* target, 
		vector& mtd)
	{
#ifdef OPTIMIZE_COLLISION
		if constexpr (
			std::same_as<std::remove_pointer_t<decltype(source)>, line_trace> &&
			std::same_as<std::remove_pointer_t<decltype(target)>, aabb>)
		{
			vector start = source->m_start;
			vector end = source->m_end;
			if (start.y == end.y)
			{
				if (start.x > end.x)
					std::swap(start.x, end.x);
				aabb source_aabb{ start.x, end.x, start.y, end.y };
				return aabb_aabb_intersect(&source_aabb, target, mtd);
			}
			else if (start.x == end.x)
			{
				if (start.y > end.y)
					std::swap(start.y, end.y);
				aabb source_aabb{ start.x, end.x, start.y, end.y };
				return aabb_aabb_intersect(&source_aabb, target, mtd);
			}
			else [[unlikely]];
		}

		if constexpr (std::same_as<std::remove_pointer_t<decltype(target)>, convex_polygon>)
		{
			if (
				target->m_collidable_type == col_slope_floor_right ||
				target->m_collidable_type == col_slope_floor_left ||
				target->m_collidable_type == col_slope_ceil_right ||
				target->m_collidable_type == col_slope_ceil_left)
			{
				aabb source_aabb;
				if constexpr (std::same_as<std::remove_pointer_t<decltype(source)>, aabb>)
					source_aabb = *source;
				if constexpr (std::same_as<std::remove_pointer_t<decltype(source)>, line_trace>)
					source_aabb = aabb{ source->m_start.x, source->m_end.x, source->m_start.y, source->m_end.y };

				aabb target_aabb{ target->m_position.x, target->m_position.x + 16.0f, target->m_position.y, target->m_position.y + 16.0f };
				if (!aabb_aabb_intersect(&source_aabb, &target_aabb, mtd))
					return false;

				float e;
				vector diag_mtd;
				if (target->m_collidable_type == col_slope_floor_right)
				{
					vector d{ source_aabb.max_x - target->m_position.x, source_aabb.max_y - target->m_position.y - 16.0f };
					e = -(d.x + d.y) / 2.0f;
					diag_mtd = vector{ e, e };
					
				}
				else if (target->m_collidable_type == col_slope_floor_left)
				{
					vector d{ source_aabb.min_x - target->m_position.x, source_aabb.max_y - target->m_position.y };
					e = (d.x - d.y) / 2.0f;
					diag_mtd = vector{ -e, e };
				}
				else if (target->m_collidable_type == col_slope_ceil_right)
				{
					vector d{ source_aabb.max_x - target->m_position.x, source_aabb.min_y - target->m_position.y };
					e = -(d.x - d.y) / 2.0f;
					diag_mtd = vector{ e, -e };
				}
				else // if (target->m_collidable_type == col_slope_ceil_left)
				{
					vector d{ source_aabb.min_x - target->m_position.x, source_aabb.min_y - target->m_position.y - 16.0f };
					e = (d.x + d.y) / 2.0f;
					diag_mtd = vector{ -e, -e };
				}
				if (!std::signbit(e))
					return false;
				if (2 * e * e < mtd.length_sqr())
					mtd = diag_mtd;
				return true;
			}
		}
#endif

		float start1 = FLOAT_MAX;
		float end1 = FLOAT_MIN;
		float start2 = FLOAT_MAX;
		float end2 = FLOAT_MIN;
		float overlap_length = 0.0f;
		float min = FLOAT_MAX;
		int32_t source_count = source->get_vertex_count();
		int32_t target_count = target->get_vertex_count();
		int32_t i = 0;
		int32_t prev_i = source_count - 1;
#ifdef OPTIMIZE_COLLISION
		if constexpr (std::same_as<std::remove_pointer_t<decltype(source)>, line_trace>)
			source_count = 1;
		if constexpr (std::same_as<std::remove_pointer_t<decltype(source)>, aabb>)
			source_count = 2;
#endif
		while (i < source_count)
		{
			vector edge = source->get_vertex(i) - source->get_vertex(prev_i);
			vector normal = vector{ -edge.y, edge.x };
			project_to_axis(normal, source, start1, end1);
			project_to_axis(normal, target, start2, end2);
			if (!do_projections_overlap(start1, end1, start2, end2, overlap_length))
				return false;
			update_mtd(normal, overlap_length, mtd, min);
			prev_i = i++;
		}
		int32_t j = 0;
		int32_t prev_j = target_count - 1;
		while (j < target_count)
		{
			vector edge = target->get_vertex(j) - target->get_vertex(prev_j);
			vector normal = vector{ -edge.y, edge.x };
			project_to_axis(normal, source, start1, end1);
			project_to_axis(normal, target, start2, end2);
			if (!do_projections_overlap(start1, end1, start2, end2, overlap_length))
				return false;
			update_mtd(normal, overlap_length, mtd, min);
			prev_j = j++;
		}
		return true;
	}

	void collision_utils::project_to_axis(vector axis, std::derived_from<i_collision_shape> auto* shape, float& start, float& end)
	{
		start = (end = shape->get_vertex(0).dot(axis));
		for (int32_t i = 1; i < shape->get_vertex_count(); i++)
		{
			float p = shape->get_vertex(i).dot(axis);
			if (p < start)
				start = p;
			else if (p > end)
				end = p;
		}
	}
}

#endif