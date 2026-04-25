#ifdef COLLISION_ENGINE_H
#include <ranges>

#include "caches.h"
#include "config.h"

namespace emu
{
	void collision_engine::get_collision_candidates(std::derived_from<i_collidable> auto* collidable)
	{
		if (collidable->get_collision() != nullptr)
		{
			caches::inst.collision_pairs.clear();
			get_collision_candidates(collidable, std::bind_front(&collision_engine::add_collision_pair, this));
		}
	}

	void collision_engine::get_collision_candidates(
		std::derived_from<i_collidable> auto* collidable,
		std::invocable<i_collidable*, i_collidable*, vector, vector, vector, vector> auto&& add_collision)
	{
		auto& query_results = caches::inst.query_results;

		aabb bounds = collidable->get_bounds();
		vector position = collidable->get_position();
		vector velocity = collidable->get_velocity();
		collision_filter collision_filter = collidable->get_collision_filter();

		query_results.clear();
		m_quad_tree.query_leaves(bounds, query_results);
		for (i_collidable* query_result : query_results)
		{
			if (
				query_result->get_collision() != nullptr &&
				collidable != query_result &&
				query_result->get_collision_filter().collides_with(collision_filter))
			{
				add_collision(
					collidable, query_result,
					position, velocity,
					query_result->get_position(), query_result->get_velocity());
			}
		}

	#ifdef OPTIMIZE_COLLISION
		if constexpr (std::same_as<std::remove_pointer_t<decltype(collidable)>, trace_line_actor>)
		{
			vector start = collidable->m_collision.m_start;
			vector end = collidable->m_collision.m_end;
			if (start.y == end.y)
			{
				if (start.x > end.x)
					std::swap(start.x, end.x);
				if (!m_level->is_colliding_hori(start, end.x - start.x))
					return;
			}
			else if (start.x == end.x)
			{
				if (start.y > end.y)
					std::swap(start.y, end.y);
				if (!m_level->is_colliding_vert(start, end.y - start.y))
					return;
			}
			else if (!m_level->is_colliding(bounds))
				return;
		}
		else
		{
			if (!m_level->is_colliding(bounds))
				return;
		}
	#endif

		auto& tile_actors = caches::inst.tile_actors;
		auto& tile_actors_count = caches::inst.tile_actors_count;

		int32_t prev_count = (int32_t)tile_actors_count;

		m_level->m_tile_layer.get_tile_actors_at(bounds, collision_filter);
		for (const auto& tile_actor : tile_actors | std::views::take(tile_actors_count) | std::views::drop(prev_count))
		{
			add_collision(
				collidable, tile_actor.get(),
				collidable->get_position(), collidable->get_velocity(),
				tile_actor->get_position(), tile_actor->get_velocity());
		}
	}
}

#endif
