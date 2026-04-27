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
	m_game_mode = emu::GameMode::standard;
	init();
}

void playground::load_challenge()
{
	// 100,000 tiles × 50 tiles tall = 20 MB tilemap.
	// Ceiling at row 2 (32 wu), floor at row 23 (368 wu) → 20-tile air gap.
	// Player spawns at X=200 tiles (3200 wu), centered in the air gap.
	emu::level::generate_corridor(m_level, 100000, 50, 2, 23, 200);
	m_session_max_speed = 0.0f;
	m_game_mode = emu::GameMode::grapple_challenge;
	init();
	m_state.no_speed_cap = true;
}

void playground::load_rg_challenge()
{
	// Same corridor as speed challenge — a long straight hallway with
	// grapple-able ceiling. The challenge is chaining RGs, not raw speed.
	emu::level::generate_corridor(m_level, 100000, 50, 2, 23, 200);
	m_game_mode = emu::GameMode::rg_challenge;
	m_rg_state = emu::RgChallengeState{};
	init();
	// Speed cap stays ON for RG mode — we care about precision, not velocity.
	m_state.no_speed_cap = false;
}

void playground::reset_rg_state()
{
	m_rg_state.reset_streak();
	m_rg_state.session_best = 0;
	m_rg_state.was_swinging_prev = false;
	m_rg_state.was_grappling_prev = false;
	m_rg_state.was_on_ground_prev = false;
	m_rg_state.was_ceiling_hit_prev = false;
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
	m_session_max_speed = 0.0f;
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

	// --- RG Challenge detection ---
	// Runs after the sim step so player state reflects the current frame.
	// UPDATED: detection window is 3 frames (99999 ticks) instead of 2.
	if (m_game_mode == emu::GameMode::rg_challenge && m_player != nullptr)
	{
		emu::player& p = *m_player;
		const bool is_swinging = p.d.is_swinging;
		const bool is_grappling = p.d.is_grappling;
		const bool is_on_ground = p.d.is_on_ground;
		const bool is_ceiling_hit = p.d.is_ceiling_hit;

		// 3 sim frames at 300 Hz = 3 * 33333 = 99999 ticks.
		constexpr uint64_t k_three_frame_ticks = 99999;

		// 1) Detect grapple SHOT direction (only when hook is flying).
		if (!m_rg_state.was_grappling_prev && is_grappling && !is_swinging)
		{
			// Grapple just fired — record direction.
			if (p.m_grapple != nullptr)
			{
				m_rg_state.grapple_was_thrown_left =
					p.m_grapple->d.direction.x < 0.0f;
			}
		}

		// 2) Detect grapple CONNECT (transition into swinging).
		if (!m_rg_state.was_swinging_prev && is_swinging)
		{
			m_rg_state.grapple_connect_time = m_state.m_time;
		}

		// 3) Detect FAILED RG — still swinging with a left grapple past 3 frames.
		if (is_swinging && m_rg_state.grapple_was_thrown_left)
		{
			const uint64_t swinging_ticks =
				(m_state.m_time - m_rg_state.grapple_connect_time).ticks;
			if (swinging_ticks > k_three_frame_ticks)
			{
				m_rg_state.reset_streak();
				m_rg_state.grapple_was_thrown_left = false;
				m_rg_state.grapple_connect_time = emu::timespan{0ull};
			}
		}

		// 4) Detect grapple RELEASE (transition out of swinging).
		if (m_rg_state.was_swinging_prev && !is_swinging && m_rg_state.grapple_was_thrown_left)
		{
			// How long was the grapple active?
			const uint64_t elapsed_ticks =
				(m_state.m_time - m_rg_state.grapple_connect_time).ticks;

			if (elapsed_ticks <= k_three_frame_ticks)
			{
				// Released within 3 frames? Check for positive vx.
				const float vx = p.m_actor->d.velocity.x;
				if (vx > 0.0f)
				{
					// RG detected!
					m_rg_state.consecutive++;
					if (m_rg_state.consecutive > m_rg_state.session_best)
						m_rg_state.session_best = m_rg_state.consecutive;
				}
			}

			// Reset per-grapple tracking.
			m_rg_state.grapple_was_thrown_left = false;
			m_rg_state.grapple_connect_time = emu::timespan{0ull};
		}

		// 5) Detect floor touch → reset streak. Ceiling hits are allowed.
		const bool just_touched_ground = !m_rg_state.was_on_ground_prev && is_on_ground;
		if (just_touched_ground)
		{
			m_rg_state.reset_streak();
		}

		// 5) Persist previous-frame state.
		m_rg_state.was_swinging_prev = is_swinging;
		m_rg_state.was_grappling_prev = is_grappling;
		m_rg_state.was_on_ground_prev = is_on_ground;
		m_rg_state.was_ceiling_hit_prev = is_ceiling_hit;
		m_rg_state.steps_since_reset++;
	}
	m_camera.viewport_size = viewport_size;
	m_camera.update(33333, m_player->m_actor->d.position);

	// Track the session's peak speed for the challenge-mode HUD.
	const float speed = m_player->m_actor->d.velocity.length();
	if (speed > m_session_max_speed) m_session_max_speed = speed;

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
		// Local player track id is "" — peers each get their own track keyed by ghost id.
		trail::record_sample("", center, a.velocity, 33333.0f * 1e-7f, m_player->d.is_using_boost);
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
