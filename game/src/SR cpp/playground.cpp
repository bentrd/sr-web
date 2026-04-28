#include <random>
#include <iostream>
#include <chrono>
#include <cmath>

#include "playground.h"
#include "emulation/player.h"
#include "emulation/tile_actor.h"
#include "emulation/rg_detector.h"
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

void run_recorder::clear()
{
	active = false;
	finished = false;
	has_been_airborne = false;
	first_tick = true;
	start_tick = 0;
	end_tick = 0;
	prev_run_end_tick = 0;
	max_speed = 0.0f;
	last_bitmask = 0;
	last_event_global_tick = 0;
	ground_streak_start_tick = 0;
	log.clear();
	// Note: was_on_ground_prev and global_tick are NOT reset — global_tick
	// is monotonic across the session and was_on_ground_prev gets refreshed
	// by the next update() before recording resumes.
}

void replay_state::clear()
{
	is_active = false;
	log.clear();
	log_pos = 0;
	tick = 0;
	duration_ticks = 0;
	next_event_tick = 0;
	next_event_bitmask = 0;
	current_bitmask = 0;
	have_event = false;
	was_on_ground_prev = true;
	has_been_airborne = false;
}

// Decode the next (varint delta, uint8 bitmask) from the log into
// next_event_tick / next_event_bitmask. Returns false on EOF / malformed
// varint — caller should clear have_event in that case.
bool replay_state::read_next_event(std::uint64_t base_tick)
{
	std::uint64_t delta = 0;
	std::size_t shift = 0;
	while (log_pos < log.size())
	{
		const std::uint8_t b = log[log_pos++];
		delta |= static_cast<std::uint64_t>(b & 0x7f) << shift;
		if ((b & 0x80) == 0)
		{
			if (log_pos >= log.size()) return false; // missing bitmask byte
			next_event_tick = base_tick + delta;
			next_event_bitmask = log[log_pos++];
			return true;
		}
		shift += 7;
		if (shift >= 64) return false; // malformed varint
	}
	return false;
}

std::uint8_t replay_state::step()
{
	// Commit any events whose tick has arrived. The first call lands at
	// tick=0 with next_event_tick=0 (the seed event), so the seed bitmask
	// is committed before we drive the first sim step.
	while (have_event && next_event_tick == tick)
	{
		current_bitmask = next_event_bitmask;
		have_event = read_next_event(next_event_tick);
	}
	const std::uint8_t bm = current_bitmask;
	tick++;
	if (tick >= duration_ticks) is_active = false;
	return bm;
}

void run_recorder::append_event(std::uint8_t bitmask)
{
	std::uint64_t delta = global_tick - last_event_global_tick;
	last_event_global_tick = global_tick;
	// LEB128 unsigned varint.
	while (delta >= 0x80)
	{
		log.push_back(static_cast<std::uint8_t>((delta & 0x7f) | 0x80));
		delta >>= 7;
	}
	log.push_back(static_cast<std::uint8_t>(delta));
	log.push_back(bitmask);
}

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
	m_run_recorder.clear();
	m_run_recorder.was_on_ground_prev = true;
	m_run_recorder.global_tick = 0;
	// Continuous recording: arm immediately on level-load and keep going
	// across multiple floor-touch events until reset / overflow.
	m_run_recorder.active = true;
	m_run_recorder.start_tick = 0;
	m_game_mode = emu::GameMode::grapple_challenge;
	init();
	m_state.no_speed_cap = true;
}

void playground::load_rg_challenge()
{
	// Same corridor as speed challenge — a long straight hallway with
	// grapple-able ceiling. The challenge is chaining RGs, not raw speed.
	emu::level::generate_corridor(m_level, 100000, 50, 2, 23, 200);
	m_run_recorder.clear();
	m_run_recorder.was_on_ground_prev = true;
	m_run_recorder.global_tick = 0;
	// Continuous recording: arm immediately so a streak-break-PR can
	// snapshot the full session log for server-side replay validation.
	m_run_recorder.active = true;
	m_run_recorder.start_tick = 0;
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
	// Hard reset drops the recording. In challenge mode the recorder is
	// re-armed immediately so the next attempt is captured from tick 0.
	m_run_recorder.clear();
	m_run_recorder.was_on_ground_prev = true;
	m_run_recorder.global_tick = 0;
	if (m_game_mode == emu::GameMode::grapple_challenge ||
		m_game_mode == emu::GameMode::rg_challenge)
	{
		m_run_recorder.active = true;
		m_run_recorder.start_tick = 0;
	}
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

bool playground::start_replay(const std::uint8_t* log_data, std::size_t len,
	std::uint64_t duration_ticks_in, int mode, std::uint64_t skip_ticks)
{
	if (log_data == nullptr || len < 2) return false;
	if (duration_ticks_in == 0) return false;
	if (skip_ticks >= duration_ticks_in) return false;

	// Lay down the same starting state the run was recorded against.
	if (mode == 0)
	{
		load_challenge();
	}
	else if (mode == 1)
	{
		load_rg_challenge();
	}
	else
	{
		return false;
	}

	// load_*() rearmed the recorder; turn it off for the duration of
	// playback so the replayed run can't be re-submitted as a PR.
	m_run_recorder.clear();
	m_run_recorder.active = false;

	m_replay.clear();
	m_replay.log.assign(log_data, log_data + len);
	m_replay.duration_ticks = duration_ticks_in;
	m_replay.tick = 0;
	m_replay.log_pos = 0;
	m_replay.have_event = m_replay.read_next_event(0);
	if (!m_replay.have_event) return false;
	m_replay.is_active = true;

	// Fast-forward: drive the sim deterministically through `skip_ticks`
	// of replay input before handing control back to the visible loop.
	// Each iteration mirrors playground::update()'s replay-driven branch
	// (resolve bitmask, write inputs, m_state.update). Camera/visuals
	// are skipped — the next live update() will resync them.
	if (skip_ticks > 0 && m_player != nullptr)
	{
		for (std::uint64_t i = 0; i < skip_ticks; ++i)
		{
			if (!m_replay.is_active) break;
			const std::uint8_t bm = m_replay.step();
			for (size_t a = 0; a < m_state.m_inputs[0].size(); ++a)
			{
				m_state.m_inputs[0][a] = ((bm >> a) & 1u) != 0;
			}
			m_state.update(33333);
			if (m_game_mode == emu::GameMode::rg_challenge && m_player != nullptr)
			{
				emu::update_rg_state(m_rg_state, *m_player, m_state.m_time);
			}
		}
	}
	return true;
}

void playground::stop_replay()
{
	m_replay.clear();
}

void playground::update(emu::timespan delta, const inputs& inputs, emu::vector viewport_size)
{
	// Resolve per-action input state and capture the bitmask in a single
	// pass — the bitmask is what drove this tick, recorded for replay.
	// While playing back a recorded run we bypass the live keyboard /
	// controller read entirely and feed the recorded bitmask into the sim
	// instead. The recorder is paused for the duration of playback (set
	// in start_replay) so a replayed run can't itself be re-submitted.
	std::uint8_t input_bitmask = 0;
	if (m_replay.is_active)
	{
		input_bitmask = m_replay.step();
		for (size_t i = 0; i < m_state.m_inputs[0].size(); i++)
		{
			m_state.m_inputs[0][i] = ((input_bitmask >> i) & 1u) != 0;
		}
	}
	else
	{
		for (size_t i = 0; i < m_state.m_inputs[0].size(); i++)
		{
			const bool pressed = inputs.held_keys[input_map[i]] || m_controller_inputs[i];
			m_state.m_inputs[0][i] = pressed;
			if (pressed) input_bitmask |= static_cast<std::uint8_t>(1u << i);
		}
	}

	m_state.update(33333);

	// --- RG Challenge detection ---
	// Detection runs after the sim step so player flags + velocity reflect
	// the current frame. The detector lives in emulation/rg_detector.cpp so
	// the server-side replay binary can call it without pulling in any
	// playground/drawing/network sources.
	if (m_game_mode == emu::GameMode::rg_challenge && m_player != nullptr)
	{
		emu::update_rg_state(m_rg_state, *m_player, m_state.m_time);
	}

	// --- Replay end-of-run detector ---
	// duration_ticks already governs the natural end (replay_state::step
	// flips is_active off when tick >= duration_ticks, which IS the PR
	// floor-touch in the recording's frame). We previously also stopped
	// on the first floor-touch-after-airborne to skip trailing content,
	// but that fired on the spawn-fall landing and clipped the replay
	// almost immediately. Revisit if we want to skip the prior-attempt
	// preamble in chained-attempt sessions — the right fix is to record
	// the last-attempt start tick alongside duration_ticks.

	// --- Challenge run recorder (continuous recording) ---
	// Active in both grapple_challenge and rg_challenge: recording starts
	// at level-load (or manual reset) and runs without interruption until
	// the next reset or 256 KB overflow. On floor-touch-after-airborne the
	// `finished` flag is raised — speed mode polls it per-frame; RG mode
	// ignores it and takes a `sr_run_snapshot` on streak-break instead.
	if ((m_game_mode == emu::GameMode::grapple_challenge ||
		 m_game_mode == emu::GameMode::rg_challenge)
		&& m_player != nullptr && m_player->m_actor != nullptr
		&& m_run_recorder.active)
	{
		auto& rec = m_run_recorder;
		rec.global_tick++;

		const bool is_on_ground = m_player->d.is_on_ground;
		const float speed = m_player->m_actor->d.velocity.length();

		if (rec.first_tick)
		{
			// Seed the log with the starting input bitmask at delta=0 so
			// the server replay knows the initial state.
			rec.first_tick = false;
			rec.start_tick = rec.global_tick;
			rec.max_speed = speed;
			rec.last_bitmask = input_bitmask;
			rec.last_event_global_tick = rec.global_tick;
			rec.append_event(input_bitmask);
		}

		if (speed > rec.max_speed) rec.max_speed = speed;

		if (input_bitmask != rec.last_bitmask)
		{
			rec.append_event(input_bitmask);
			rec.last_bitmask = input_bitmask;
		}

		// Track airborne state — floor touch only fires `finished` after
		// the player has actually left the ground at least once since the
		// last submission, so spawn-on-floor doesn't immediately submit.
		if (!is_on_ground) rec.has_been_airborne = true;

		// Run-end trigger: the player must be on the ground AND not in a
		// swing/grapple state continuously for k_ground_grace_ticks.
		// Brief floor-grazes mid-swing don't reset the streak only if
		// they happen during a grapple — but a graze WHILE grappling is
		// still "on ground" for one tick, so we exclude swing/grapple
		// from counting toward the streak entirely. end_tick is set to
		// the moment of first landing so the replay terminates there
		// rather than 0.5s later.
		const bool in_swing = m_player->d.is_grappling || m_player->d.is_swinging;
		if (!is_on_ground || in_swing)
		{
			rec.ground_streak_start_tick = 0;
		}
		else if (rec.ground_streak_start_tick == 0)
		{
			rec.ground_streak_start_tick = rec.global_tick;
		}

		if (rec.has_been_airborne
			&& rec.ground_streak_start_tick != 0
			&& rec.global_tick >= rec.ground_streak_start_tick
				+ static_cast<std::uint64_t>(run_recorder::k_ground_grace_ticks - 1))
		{
			// Capture the OLD end_tick before overwriting it — that's the
			// start of the attempt that just ended (used by replays to
			// skip earlier attempts in chained sessions). end_tick is the
			// landing tick, not the grace-expiry tick, so the visible
			// replay ends the moment the player lands.
			rec.prev_run_end_tick = rec.end_tick;
			rec.end_tick = rec.ground_streak_start_tick;
			rec.finished = true;
			rec.has_been_airborne = false;
			rec.ground_streak_start_tick = 0;
		}

		// Hard cap on payload — drop the recorder rather than letting the
		// log grow unbounded. JS treats this as a forced reset.
		if (rec.log.size() > run_recorder::k_log_max_bytes)
		{
			rec.active = false;
		}

		rec.was_on_ground_prev = is_on_ground;
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

void playground::reset_controller_inputs()
{
	m_controller_inputs.reset();
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
