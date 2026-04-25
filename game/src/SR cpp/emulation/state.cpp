#include "state.h"
#include "player.h"
#include "grapple.h"
#include "player_start.h"
#include "super_boost_volume.h"
#include "boost_section.h"
#include "obstacle.h"

using namespace emu;

state::state()
{
	
}

state::state(level& level)
{
	m_collision_engine.m_level = &level;
	m_collision_engine.set_world_bounds(aabb{ 0, level.m_tile_layer.get_width() * 16.0f, -500, level.m_tile_layer.get_height() * 16.0f });

	for (const level_actor& def : level.m_actors)
	{
		if (def.type == "SuperBoostVolume")
			spawn<super_boost_volume>(def);
		else if (def.type == "BoostSection")
			spawn<boost_section>(def);
		else if (def.type == "Obstacle")
			spawn<obstacle>(def);
	}

	player* player = spawn<emu::player>({ 0.0f, 0.0f });

	const level_actor& start_position = level.get_actor("PlayerStart");
	player->m_actor->set_position(start_position.position);
}

state::state(const state& right) :
	m_next_actor_id{ right.m_next_actor_id },
	m_collision_engine{ right.m_collision_engine },
	m_time{ right.m_time },
	m_inputs{ right.m_inputs }
{
	for (auto& actor : m_collision_engine.m_actors)
		actor->m_state = this;
}

state::state(state&& right) noexcept :
	m_next_actor_id{ right.m_next_actor_id },
	m_collision_engine{ std::move(right.m_collision_engine) },
	m_time{ right.m_time },
	m_inputs{ right.m_inputs }
{
	for (auto& actor : m_collision_engine.m_actors)
		actor->m_state = this;
}

state& state::operator=(const state& right)
{
	m_next_actor_id = right.m_next_actor_id;
	m_collision_engine = right.m_collision_engine;
	m_time = right.m_time;
	m_inputs = right.m_inputs;
	for (auto& actor : m_collision_engine.m_actors)
		actor->m_state = this;

	return *this;
}

state& state::operator=(state&& right) noexcept
{
	m_next_actor_id = right.m_next_actor_id;
	m_collision_engine = std::move(right.m_collision_engine);
	m_time = right.m_time;
	m_inputs = right.m_inputs;
	for (auto& actor : m_collision_engine.m_actors)
		actor->m_state = this;

	return *this;
}

std::vector<std::unique_ptr<actor>>& state::actors()
{
	return m_collision_engine.m_actors;
}

const std::vector<std::unique_ptr<actor>>& state::actors() const
{
	return m_collision_engine.m_actors;
}

void state::update(timespan delta)
{
	m_collision_engine.clear_collisions();
#ifdef OPTIMIZE_COLLISION
	for (auto& actor : m_collision_engine.m_actors_has_update)
#else
	for (auto& actor : m_collision_engine.m_actors)
#endif
		actor->update(m_time, delta);
	m_collision_engine.update_collisions((float)delta.seconds());
#ifdef OPTIMIZE_COLLISION
	for (auto& actor : m_collision_engine.m_actors_is_movable)
#else
	for (auto& actor : m_collision_engine.m_actors)
#endif
	{
		actor->resolve_collision(m_time, delta);
	}
	m_time.ticks += delta;
}
