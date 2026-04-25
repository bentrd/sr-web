#include <random>
#include <iostream>
#include <chrono>

#include "playground.h"
#include "emulation/player.h"
#include "emulation/tile_actor.h"
#include "drawing/draw_util.h"

std::array<int, emu::input_count> input_map
{
	GLFW_KEY_A,           // left
	GLFW_KEY_D,           // right
	GLFW_KEY_SPACE,       // jump
	GLFW_KEY_W,           // grapple
	GLFW_KEY_S,           // slide
	GLFW_KEY_LEFT_SHIFT,  // boost
	GLFW_KEY_E,           // item
	GLFW_KEY_F            // swap item
};

playground::playground() = default;

void playground::load(const std::string& map_path)
{
	m_level = emu::level{ map_path.c_str() };
	init();
}

void playground::init()
{
	m_state = emu::state{ m_level };
	//m_prep = util::level_prep{ m_level };
	m_helper = util::get_event_helper(*m_state.get_contr<emu::player>(0));
	m_player = m_state.get_contr<emu::player>(0);
}

void playground::reset()
{
	if (m_player != nullptr)
	{
		m_player->reset();
		m_player->m_actor->set_position(m_level.get_actor("PlayerStart").position);
		m_helper = util::get_event_helper(*m_player);
	}
}

void playground::update_input(const inputs&)
{
	// All editor / debug input handling (mouse paint, R reset, F/G step,
	// Pause toggle) was removed for the web build — this is play-only.
}

void playground::update(emu::timespan delta, const inputs& inputs, emu::vector viewport_size)
{
	for (size_t i = 0; i < m_state.m_inputs[0].size(); i++)
		m_state.m_inputs[0][i] = inputs.held_keys[input_map[i]];

	m_state.update(33333);
	m_camera.viewport_size = viewport_size;
	m_camera.update(33333, m_player->m_actor->d.position);

	util::event event = m_helper.get_event(*m_player);
	m_last_event = event.evt;
}

void playground::draw(const inputs&)
{
	if (m_local_identity.is_set)
		draw::set_local_player_color(m_local_identity.r, m_local_identity.g, m_local_identity.b);

	draw::draw_state(&m_state, m_camera);

	// Ghosts composite over world + local player at 50% alpha. They are
	// never added to state.actors() — see AGENTS.md "Ghosts are render-only".
	auto ghosts = m_ghosts.snapshot();
	for (const auto& [id, ghost] : ghosts)
		draw::draw_ghost(ghost, m_camera);

	// Flush all batched primitives in a handful of draw calls. See
	// drawing/draw_util.cpp — this is the load-bearing perf optimization
	// (was thousands of glDrawArrays per frame before batching).
	draw::flush_frame();
}
