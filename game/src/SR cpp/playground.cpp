#include <random>
#include <iostream>
#include <chrono>
#include <cmath>

#include "playground.h"
#include "emulation/player.h"
#include "emulation/tile_actor.h"
#include "drawing/draw_util.h"
#include "drawing/trail.h"

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

	// 33333 = .NET TimeSpan ticks (100ns each) — see AGENTS.md. The trail
	// subsystem throttles internally to ~60 Hz, so feeding it the raw 300 Hz
	// sim cadence costs nothing extra and keeps the timing model uniform
	// regardless of any future changes to the sim rate.
	if (m_player != nullptr && m_player->m_actor != nullptr)
	{
		const auto& a = m_player->m_actor->d;
		// Anchor at the player's body center so the ribbon attaches under
		// the rectangle rather than the top-left corner.
		emu::vector center{ a.position.x + a.size.x * 0.5f, a.position.y + a.size.y * 0.5f };
		trail::record_sample(center, a.velocity, 33333.0f * 1e-7f, m_player->d.is_using_boost);
	}

	util::event event = m_helper.get_event(*m_player);
	m_last_event = event.evt;
}

void playground::draw(const inputs&)
{
	if (m_local_identity.is_set)
		draw::set_local_player_color(m_local_identity.r, m_local_identity.g, m_local_identity.b);

	// World pass: tiles + non-player actors (grapple ropes, boost
	// volumes, obstacles). Flush before drawing the trail so the trail's
	// own shader run sits on top of the world batch.
	draw::draw_state_world(&m_state, m_camera);
	draw::flush_frame();

	// Trail goes between world and player so it appears behind the
	// rectangle, matching SpeedRunners' "TrailBehindLocalPlayersLayer".
	// |vel.x| drives the per-frame fade for ONLY_AT_SUPERSPEED layers —
	// passing it here (rather than caching at sample time) means the
	// trail vanishes the instant the player drops below the gate, even
	// while old samples are still inside their lifetime.
	const float live_abs_vx = (m_player != nullptr && m_player->m_actor != nullptr)
		? std::abs(m_player->m_actor->d.velocity.x)
		: 0.0f;
	trail::draw_all(m_camera, live_abs_vx);

	// Player pass + ghosts. Single flush at the end ties them all into
	// one batch (still two glDrawArrays — triangles + lines).
	draw::draw_state_players(&m_state, m_camera);
	auto ghosts = m_ghosts.snapshot();
	for (const auto& [id, ghost] : ghosts)
		draw::draw_ghost(ghost, m_camera);
	draw::flush_frame();
}
