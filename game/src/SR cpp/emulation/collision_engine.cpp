#include <ranges>

#include "common.h"
#include "collision_engine.h"
#include "collision_utils.h"
#include "tile_layer_base.h"
#include "caches.h"

#include "config.h"

using namespace emu;

collision_engine::collision_engine()
{
	m_actors.reserve(128);
	m_auto_col_det_actors.reserve(128);
#ifdef OPTIMIZE_COLLISION
	m_actors_has_update.reserve(128);
	m_actors_is_movable.reserve(128);
#endif
}

collision_engine::collision_engine(const collision_engine& right) :
	m_quad_tree{ right.m_quad_tree },
	m_level{ right.m_level }
{
	// be careful when cloning the collision engine to correctly clone
	// all its members and pointers referencing them

	std::map<const actor*, actor*> leaf_map;
	std::map<const i_actor_controller*, i_actor_controller*> contr_map;

	m_actors.reserve(128);
	m_auto_col_det_actors.reserve(128);
#ifdef OPTIMIZE_COLLISION
	m_actors_has_update.reserve(128);
	m_actors_is_movable.reserve(128);
#endif

	for (const auto& actor : right.m_actors)
	{
		::actor* new_actor = m_actors.emplace_back(::clone<::actor>(actor.get())).get();
		leaf_map.emplace(actor.get(), new_actor);
		contr_map.emplace(actor->m_controller.get(), new_actor->m_controller.get());
		if (new_actor->d.automatic_collision_detection)
			m_auto_col_det_actors.push_back(new_actor);
#ifdef OPTIMIZE_COLLISION
		if (new_actor->m_has_update)
			m_actors_has_update.push_back(new_actor);
		if (new_actor->m_is_movable)
			m_actors_is_movable.push_back(new_actor);
#endif
	}

	m_quad_tree.replace_pointers(leaf_map);

	for (const auto& actor : m_actors)
		actor->m_controller->replace_pointers(contr_map);
}

collision_engine& collision_engine::operator=(const collision_engine& right)
{
	// be careful when cloning the collision engine to correctly clone
	// all its members and pointers referencing them

	m_level = right.m_level;

	std::map<const actor*, actor*> leaf_map;
	std::map<const i_actor_controller*, i_actor_controller*> contr_map;

	m_auto_col_det_actors.clear();
#ifdef OPTIMIZE_COLLISION
	m_actors_has_update.clear();
	m_actors_is_movable.clear();
#endif

	auto it = m_actors.begin();
	auto it_r = right.m_actors.begin();

	// reuse actors when possible
	for (; it != m_actors.end() && it_r != right.m_actors.end(); ++it, ++it_r)
	{
		*(*it) = *(*it_r);
		actor* actor = it->get();
		leaf_map.emplace(it_r->get(), actor);
		contr_map.emplace((*it_r)->m_controller.get(), actor->m_controller.get());
		if (actor->d.automatic_collision_detection)
			m_auto_col_det_actors.push_back(actor);
#ifdef OPTIMIZE_COLLISION
		if (actor->m_has_update)
			m_actors_has_update.push_back(actor);
		if (actor->m_is_movable)
			m_actors_is_movable.push_back(actor);
#endif
	}
	if (it == m_actors.end())
	{
		for (; it_r != right.m_actors.end(); ++it_r)
		{
			actor* new_actor = m_actors.emplace_back(::clone<actor>(it_r->get())).get();
			leaf_map.emplace(it_r->get(), new_actor);
			contr_map.emplace((*it_r)->m_controller.get(), new_actor->m_controller.get());
			if (new_actor->d.automatic_collision_detection)
				m_auto_col_det_actors.push_back(new_actor);
#ifdef OPTIMIZE_COLLISION
			if (new_actor->m_has_update)
				m_actors_has_update.push_back(new_actor);
			if (new_actor->m_is_movable)
				m_actors_is_movable.push_back(new_actor);
#endif
		}
	}
	else
		m_actors.erase(it, m_actors.end());

	m_quad_tree = right.m_quad_tree;
	m_quad_tree.replace_pointers(leaf_map);

	for (const auto& actor : m_actors)
		actor->m_controller->replace_pointers(contr_map);

	return *this;
}

void collision_engine::clear()
{
	m_quad_tree.clear();
	m_actors.clear();
	m_auto_col_det_actors.clear();
#ifdef OPTIMIZE_COLLISION
	m_actors_has_update.clear();
	m_actors_is_movable.clear();
#endif
}

void collision_engine::reset_actors()
{
	for (auto& actor : m_actors)
		actor->get_controller()->reset();
}

void collision_engine::add_actor(std::unique_ptr<actor>&& actor)
{
	::actor* actor_ptr = actor.get();
	m_actors.emplace_back(std::move(actor));
	if (actor_ptr->d.automatic_collision_detection)
		m_auto_col_det_actors.push_back(actor_ptr);
#ifdef OPTIMIZE_COLLISION
	if (actor_ptr->m_has_update)
		m_actors_has_update.push_back(actor_ptr);
	if (actor_ptr->m_is_movable)
		m_actors_is_movable.push_back(actor_ptr);
#endif
	if (actor_ptr->d.is_collidable)
		m_quad_tree.add(actor_ptr);
}

void collision_engine::remove_actor(actor* actor)
{
	if (actor->d.is_collidable)
	{
		m_quad_tree.remove(actor);
	}
	if (actor->d.automatic_collision_detection)
	{
		auto it2 = std::ranges::find(m_auto_col_det_actors, actor);
		if (it2 != m_auto_col_det_actors.end())
			m_auto_col_det_actors.erase(it2);
	}
#ifdef OPTIMIZE_COLLISION
	if (actor->m_has_update)
	{
		auto it2 = std::ranges::find(m_actors_has_update, actor);
		if (it2 != m_actors_has_update.end())
			m_actors_has_update.erase(it2);
	}
	if (actor->m_is_movable)
	{
		auto it2 = std::ranges::find(m_actors_is_movable, actor);
		if (it2 != m_actors_is_movable.end())
			m_actors_is_movable.erase(it2);
	}
#endif

	auto it1 = std::ranges::find_if(m_actors, [actor](const auto& u_ptr)
		{
			return actor == u_ptr.get();
		});
	if (it1 != m_actors.end())
		m_actors.erase(it1);
}

void collision_engine::swap_actors(actor* actor1, actor* actor2)
{
	auto it1 = std::ranges::find_if(m_actors, [actor1](const auto& u_ptr)
		{
			return actor1 == u_ptr.get();
		});
	auto it2 = std::ranges::find_if(m_actors, [actor2](const auto& u_ptr)
		{
			return actor2 == u_ptr.get();
		});

	if (it1 != m_actors.end() && it2 != m_actors.end())
	{
		std::swap(*it1, *it2);
	}
}

int32_t collision_engine::get_actor_count() const
{
	return (int32_t)m_actors.size();
}

actor* collision_engine::get_actor(int32_t index)
{
	return m_actors[index].get();
}

actor* collision_engine::get_actor_by_id(int32_t id)
{
	auto it = std::ranges::find_if(m_actors, [id](const auto& actor)
		{
			return actor->d.id == id;
		});
	if (it != m_actors.end())
		return it->get();

	return nullptr;
}

void collision_engine::add_collision_pair(
	i_collidable* source, i_collidable* target,
	vector source_pos, vector source_vel,
	vector target_pos, vector target_vel)
{
	caches::inst.collision_pairs.emplace_back(source, target, source_pos, source_vel, target_pos, target_vel);
}

void collision_engine::set_world_bounds(const aabb& a1)
{
	m_quad_tree = quad_tree_node{ a1 };
	for (auto& actor : m_actors)
	{
		if (actor != nullptr && actor->d.is_collidable)
			m_quad_tree.add(actor.get());
	}
}

int32_t collision_engine::trace(vector start_point, vector end_point, std::vector<collision_pair*>& collisions)
{
	return trace(start_point, end_point, filter_all, collisions);
}

int32_t collision_engine::trace(vector start_point, vector end_point, collision_filter filter, std::vector<collision_pair*>& collisions)
{
	auto& trace_line_actors = caches::inst.trace_line_actors;
	auto& trace_line_actors_count = caches::inst.trace_line_actors_count;

	if (trace_line_actors.size() == trace_line_actors_count)
		trace_line_actors.resize(trace_line_actors_count + 1);

	auto& trace_line_actor = trace_line_actors[trace_line_actors_count++];

	if (trace_line_actor == nullptr)
		trace_line_actor = std::make_unique<::trace_line_actor>(start_point, end_point, filter);
	else
		trace_line_actor->set(start_point, end_point, filter);

	get_collision_candidates(trace_line_actor.get());
	return check_collision_candidates_from_buf(collisions);
}

collision_pair* collision_engine::trace_single(vector start_point, vector end_point)
{
	return trace_single(start_point, end_point, filter_all, 50.0f);
}

collision_pair* collision_engine::trace_single(vector start_point, vector end_point, collision_filter filter)
{
	return trace_single(start_point, end_point, filter, 50.0f);
}

collision_pair* collision_engine::trace_single(vector start_point, vector end_point, collision_filter filter, float segment_length)
{
	vector next_segment_start = start_point;
	vector delta = end_point - start_point;
	float remaining_length = delta.length();
	delta = delta / remaining_length;
	while (remaining_length > 0.0f)
	{
		vector segment_start = next_segment_start;
		next_segment_start = segment_start + delta * std::min(remaining_length, segment_length);
		remaining_length -= segment_length;

		auto& trace_line_actors = caches::inst.trace_line_actors;
		auto& trace_line_actors_count = caches::inst.trace_line_actors_count;

		if (trace_line_actors.size() == trace_line_actors_count)
			trace_line_actors.resize(trace_line_actors_count + 1);

		auto& trace_line_actor = trace_line_actors[trace_line_actors_count++];

		if (trace_line_actor == nullptr)
			trace_line_actor = std::make_unique<::trace_line_actor>(segment_start, next_segment_start, filter);
		else
			trace_line_actor->set(segment_start, next_segment_start, filter);

		get_collision_candidates(trace_line_actor.get());
		int32_t candidates_count = check_collision_candidates_from_buf(caches::inst.collision_pairs_ptr);
		if (candidates_count > 0)
		{
			collision_pair* result = caches::inst.collision_pairs_ptr[0];
			float min_distance = FLOAT_MAX;
			for (collision_pair* collision : caches::inst.collision_pairs_ptr | std::views::take(candidates_count))
			{
				float distance = (collision->m_target->get_collision()->get_center() - segment_start).length_sqr();
				if (distance < min_distance)
				{
					result = collision;
					min_distance = distance;
				}
			}
			return result;
		}
	}
	return nullptr;
}

int32_t collision_engine::check_shape(const aabb& bounds, i_collision_shape& collision, std::vector<collision_pair*>& collisions)
{
	return check_shape(bounds, collision, filter_all, collisions);
}

int32_t collision_engine::check_shape(
	const aabb& bounds, 
	i_collision_shape& collision, 
	collision_filter filter, 
	std::vector<collision_pair*>& collisions)
{
	auto& trace_polygon_actors = caches::inst.trace_polygon_actors;
	auto& trace_polygon_actors_count = caches::inst.trace_polygon_actors_count;

	if (trace_polygon_actors.size() == trace_polygon_actors_count)
		trace_polygon_actors.resize(trace_polygon_actors_count + 1);

	auto& trace_polygon_actor = trace_polygon_actors[trace_polygon_actors_count++];

	if (trace_polygon_actor == nullptr)
		trace_polygon_actor = std::make_unique<::trace_polygon_actor>(bounds, &collision, filter);
	else
		trace_polygon_actor->set(bounds, &collision, filter);

	get_collision_candidates(trace_polygon_actor.get());
	return check_collision_candidates_from_buf(collisions);
}

void collision_engine::refresh_collisions_on_actor(actor* actor)
{
	actor->reset_collision(0.0f, false);

	if (actor->d.is_collidable)
		m_quad_tree.update(actor);

	actor->reset_changed();

	int32_t collisions_on_actor = get_collisions_on_actor(actor, caches::inst.collision_pairs_ptr);
	for (collision_pair* collision : caches::inst.collision_pairs_ptr | std::views::take(collisions_on_actor))
		actor->add_collision(*collision);
}

void collision_engine::refresh_collisions_on_actor(actor* actor, float remaining_delta_time)
{
	actor->reset_collision(remaining_delta_time, false);

	if (actor->d.is_collidable)
		m_quad_tree.update(actor);

	actor->reset_changed();
	
	get_collision_candidates(actor, std::bind_front(static_cast<collision_pair&(actor::*)(i_collidable*, i_collidable*, vector, vector, vector, vector)>(&actor::add_collision), actor));
	check_collision_candidates_actor(actor, remaining_delta_time);
}

int32_t collision_engine::get_collisions_on_actor(actor* actor, std::vector<collision_pair*>& collisions)
{
	get_collision_candidates(actor);
	return check_collision_candidates_from_buf(collisions);
}

void collision_engine::clear_collisions()
{
	caches::inst.collision_pairs.clear();
	caches::inst.tile_actors_count = 0;
	caches::inst.trace_line_actors_count = 0;
	caches::inst.trace_polygon_actors_count = 0;
}

void collision_engine::update_collisions(float delta_s)
{
#ifdef OPTIMIZE_COLLISION
	for (auto& actor : m_actors_is_movable)
#else
	for (auto& actor : m_actors)
#endif
	{
		if (actor != nullptr && actor->d.is_collidable)
		{
			actor->reset_collision(delta_s, true);
			actor->reset_changed();

			if (actor->is_moving())
				m_quad_tree.update(&*actor);
		}
	}
	get_collision_candidates_all_actors();
	check_collision_candidates_all_actors(delta_s);
}

void collision_engine::get_collision_candidates_all_actors()
{
	for (actor* actor : m_auto_col_det_actors)
	{
		if (actor->get_collision() != nullptr && actor->d.is_collision_active)
		{
			get_collision_candidates(actor, std::bind_front(static_cast<collision_pair&(actor::*)(i_collidable*, i_collidable*, vector, vector, vector, vector)>(&actor::add_collision), actor));
		}
	}
}

void collision_engine::check_collision_candidates_all_actors(float a1)
{
	for (actor* actor : m_auto_col_det_actors)
	{
		if (actor->get_collision_count() > 0)
			check_collision_candidates_actor(actor, a1);
	}
}

void collision_engine::check_collision_candidates_actor(actor* actor, float delta_s)
{
	float collision_sweep_minimal_density = actor->d.collision_sweep_minimal_density;
	int32_t sweep_count;
	float iteration_time_step;
	if (actor->d.should_predict_collision && actor->get_velocity().length_sqr() > 0.0f)
	{
		if (std::abs(actor->get_velocity().normalized().x) < actor->get_size().normalized().x)
		{
			sweep_count = (int32_t)std::ceil((double)(std::abs(actor->get_velocity().y * delta_s) / actor->get_size().y * collision_sweep_minimal_density));
		}
		else
		{
			sweep_count = (int32_t)std::ceil((double)(std::abs(actor->get_velocity().x * delta_s) / actor->get_size().x * collision_sweep_minimal_density));
		}
		iteration_time_step = delta_s / sweep_count;
	}
	else
	{
		sweep_count = 0;
		iteration_time_step = delta_s;
	}
	// ignore error handling code
	actor->d.total_iterations = sweep_count + 1;
	actor->d.iteration_time_step = iteration_time_step;
	i_collision_shape* collision_shape = actor->get_collision();
	vector original_position = collision_shape->get_position();
	int32_t collision_count = actor->get_collision_count();
	for (int32_t i = 0; i <= sweep_count; i++)
	{
		collision_shape->set_position(original_position + actor->get_velocity() * (float)i * iteration_time_step);
		for (int32_t j = 0; j < collision_count; j++)
		{
			collision_pair* collision = &actor->m_collisions[j];
			i_collidable* target = collision->m_target;
			i_collision_shape* target_collision_shape = target->get_collision();
			vector original_target_position = target_collision_shape->get_position();
			target_collision_shape->set_position(original_target_position + collision->m_target_vel * (float)i * iteration_time_step);
			if (i > 0)
			{
				collision = &actor->add_collision(
					actor, target, 
					collision_shape->get_position(), actor->get_velocity(), 
					target_collision_shape->get_position(), target->get_velocity()
				);
			}
			collision->m_iteration = i;
			collision->m_is_colliding = collision_utils::intersect(collision_shape, target_collision_shape, collision->m_mtd);
			target_collision_shape->set_position(original_target_position);
		}
	}
	collision_shape->set_position(original_position);
}

int32_t collision_engine::check_collision_candidates_from_buf(std::vector<collision_pair*>& colliding)
{
	auto& collision_pairs = caches::inst.collision_pairs;

	if (colliding.size() < collision_pairs.size())
		colliding.resize(collision_pairs.size());
	int32_t result = 0;
	for (collision_pair& collision : collision_pairs)
	{
		i_collision_shape* source_collision_shape = collision.m_source->get_collision();
		i_collision_shape* target_collision_shape = collision.m_target->get_collision();
		collision.m_is_colliding = collision_utils::intersect(source_collision_shape, target_collision_shape, collision.m_mtd);
		if (collision.m_is_colliding/* && collision.m_mtd.length_sqr() >= 0.0f*/) // why ???
			colliding[result++] = &collision;
		// ignore error handling code
	}
	return result;
}