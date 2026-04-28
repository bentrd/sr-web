#include <random>
#include <iostream>
#include <chrono>
#include <cmath>
#include <cstring>

#include "playground.h"
#include "emulation/player.h"
#include "emulation/grapple.h"
#include "emulation/tile_actor.h"
#include "emulation/rg_detector.h"
#include "drawing/draw_util.h"
#include "drawing/trail.h"
#include "drawing/visuals_config.h"

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
	has_been_grounded = false;
	has_been_airborne = false;
	first_tick = true;
	start_tick = 0;
	end_tick = 0;
	max_speed = 0.0f;
	max_streak = 0;
	last_bitmask = 0;
	last_event_global_tick = 0;
	ground_streak_start_tick = 0;
	prev_rg_consecutive = 0;
	log.clear();
	savestate.clear();
	savestate_size = 0;
	// Note: global_tick / was_on_ground_prev / was_on_ground_prev_rg are
	// reset by load_*() / reset() / re-arm — we don't reset them here so
	// in-flight callers see a stable state until the recorder rearms.
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
	m_game_mode = emu::GameMode::grapple_challenge;
	init();
	m_state.no_speed_cap = true;
	// init() spawned the player at PlayerStart. Now capture the savestate
	// for the first run — see arm_recorder for the per-run lifecycle.
	arm_recorder();
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
	arm_recorder();
}

void playground::load_time_challenge()
{
	// 30,000 wu corridor (1875 tiles × 16 wu). Player spawns at column 1
	// — flush against the left wall — and races to touch the right wall.
	// Same vertical structure as the other challenges so the grapple
	// ceiling and floor read identically.
	emu::level::generate_corridor(m_level,
		emu::time_challenge::corridor_width_tiles,
		emu::time_challenge::corridor_height_tiles,
		emu::time_challenge::ceil_y,
		emu::time_challenge::floor_y,
		emu::time_challenge::start_x,
		/*spawn_on_ground=*/true);
	m_session_max_speed = 0.0f;
	m_game_mode = emu::GameMode::time_challenge;
	init();
	// Speed cap off — players need raw velocity to traverse 30K wu fast.
	m_state.no_speed_cap = true;
	arm_recorder();
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
	m_run_recorder.clear();
	if (m_player != nullptr)
	{
		m_player->reset();
		m_player->m_actor->set_position(m_level.get_actor("PlayerStart").position);
		m_helper = util::get_event_helper(*m_player);
	}
	// In challenge mode rearm the recorder against the freshly-reset
	// player so the next attempt is captured from tick 0.
	if (m_game_mode == emu::GameMode::grapple_challenge ||
		m_game_mode == emu::GameMode::rg_challenge ||
		m_game_mode == emu::GameMode::time_challenge)
	{
		arm_recorder();
	}
}

void playground::update_input(const inputs&)
{
	// All editor / debug input handling (mouse paint, R reset, F/G step,
	// Pause toggle) was removed for the web build — this is play-only.
}

bool playground::start_replay(const std::uint8_t* log_data, std::size_t len,
	std::uint64_t duration_ticks_in, int mode,
	const std::uint8_t* savestate_data, std::size_t savestate_len)
{
	if (log_data == nullptr || len < 2) return false;
	if (duration_ticks_in == 0) return false;
	if (savestate_data == nullptr || savestate_len == 0) return false;

	// Lay down the deterministic level + freshly-spawned player.
	if (mode == 0)
	{
		load_challenge();
	}
	else if (mode == 1)
	{
		load_rg_challenge();
	}
	else if (mode == 2)
	{
		load_time_challenge();
	}
	else
	{
		return false;
	}

	// Restore the player to the exact mid-session pose the run started
	// from. Without this the replay would diverge whenever the original
	// recording started somewhere other than PlayerStart (e.g. the second
	// run after a finished first run, where the player is mid-air).
	if (!restore_savestate(savestate_data, savestate_len))
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

	return true;
}

void playground::stop_replay()
{
	m_replay.clear();
}

namespace
{
	// Savestate header layout. Sizes are validated on restore against the
	// locally-built sizeof()s — any mismatch (different sim_version, different
	// build) rejects the savestate rather than memcpy'ing garbage.
	struct savestate_header
	{
		std::uint32_t magic;            // k_savestate_magic
		std::uint32_t version;          // k_savestate_version
		std::uint32_t player_d_size;    // sizeof(emu::player::d)
		std::uint32_t actor_d_size;     // sizeof(emu::actor::d)
		std::uint32_t grapple_d_size;   // sizeof(emu::grapple::d) (0 if absent)
		std::uint32_t grapple_actor_d_size; // sizeof(emu::actor::d) (0 if absent)
		std::uint32_t rg_state_size;    // sizeof(emu::RgChallengeState)
		std::uint32_t flags;            // bit 0: has_grapple
		std::int64_t state_time_ticks;  // .NET TimeSpan ticks (100ns each)
		std::uint64_t reserved;         // pad to 48 bytes for alignment
	};
	static_assert(sizeof(savestate_header) == 48, "savestate header must be 48 bytes");
}

std::size_t playground::capture_savestate(std::uint8_t* out, std::size_t out_size)
{
	if (out == nullptr || m_player == nullptr || m_player->m_actor == nullptr) return 0;

	const std::size_t player_sz = sizeof(m_player->d);
	const std::size_t actor_sz = sizeof(m_player->m_actor->d);
	const std::size_t rg_sz = sizeof(emu::RgChallengeState);

	const bool has_grapple = (m_player->m_grapple != nullptr)
		&& (m_player->m_grapple->m_actor != nullptr);
	const std::size_t grapple_sz = has_grapple ? sizeof(m_player->m_grapple->d) : 0u;
	const std::size_t grapple_actor_sz = has_grapple
		? sizeof(m_player->m_grapple->m_actor->d) : 0u;

	const std::size_t total = sizeof(savestate_header) + player_sz + actor_sz
		+ grapple_sz + grapple_actor_sz + rg_sz;
	if (out_size < total) return 0;

	savestate_header h{};
	h.magic = k_savestate_magic;
	h.version = k_savestate_version;
	h.player_d_size = static_cast<std::uint32_t>(player_sz);
	h.actor_d_size = static_cast<std::uint32_t>(actor_sz);
	h.grapple_d_size = static_cast<std::uint32_t>(grapple_sz);
	h.grapple_actor_d_size = static_cast<std::uint32_t>(grapple_actor_sz);
	h.rg_state_size = static_cast<std::uint32_t>(rg_sz);
	h.flags = has_grapple ? 1u : 0u;
	h.state_time_ticks = static_cast<std::int64_t>(m_state.m_time.ticks);
	h.reserved = 0;

	std::size_t off = 0;
	std::memcpy(out + off, &h, sizeof(h)); off += sizeof(h);
	std::memcpy(out + off, &m_player->d, player_sz); off += player_sz;
	std::memcpy(out + off, &m_player->m_actor->d, actor_sz); off += actor_sz;
	if (has_grapple)
	{
		std::memcpy(out + off, &m_player->m_grapple->d, grapple_sz); off += grapple_sz;
		std::memcpy(out + off, &m_player->m_grapple->m_actor->d, grapple_actor_sz); off += grapple_actor_sz;
	}
	std::memcpy(out + off, &m_rg_state, rg_sz); off += rg_sz;
	return off;
}

bool playground::arm_recorder()
{
	auto& rec = m_run_recorder;
	rec.clear();
	if (m_player == nullptr || m_player->m_actor == nullptr)
	{
		rec.active = false;
		return false;
	}
	// Capture the savestate against the CURRENT player pose. The next
	// sim step (driven by playground::update) advances this state with
	// the seed input bitmask, which is what the server replay does too.
	rec.savestate.assign(64 * 1024, 0u);
	rec.savestate_size = capture_savestate(rec.savestate.data(), rec.savestate.size());
	if (rec.savestate_size == 0)
	{
		rec.savestate.clear();
		rec.active = false;
		return false;
	}
	rec.savestate.resize(rec.savestate_size);

	rec.active = true;
	rec.first_tick = true;
	rec.start_tick = 0;
	rec.global_tick = 0;
	rec.last_event_global_tick = 0;
	rec.was_on_ground_prev = m_player->d.is_on_ground;
	rec.was_on_ground_prev_rg = m_player->d.is_on_ground;
	rec.has_been_grounded = m_player->d.is_on_ground;
	rec.has_been_airborne = false;
	rec.prev_rg_consecutive = m_rg_state.consecutive;
	rec.max_streak = m_rg_state.session_best;
	// Time challenge defers the first tick until the player issues input.
	// Speed/RG modes start counting immediately (matches prior behavior).
	rec.waiting_for_input = (m_game_mode == emu::GameMode::time_challenge);
	return true;
}

bool playground::restore_savestate(const std::uint8_t* in, std::size_t in_size)
{
	if (in == nullptr || in_size < sizeof(savestate_header)) return false;
	if (m_player == nullptr || m_player->m_actor == nullptr) return false;

	savestate_header h{};
	std::memcpy(&h, in, sizeof(h));
	if (h.magic != k_savestate_magic) return false;
	if (h.version != k_savestate_version) return false;
	if (h.player_d_size != sizeof(m_player->d)) return false;
	if (h.actor_d_size != sizeof(m_player->m_actor->d)) return false;
	if (h.rg_state_size != sizeof(emu::RgChallengeState)) return false;

	const bool has_grapple = (h.flags & 1u) != 0u;
	if (has_grapple)
	{
		if (m_player->m_grapple == nullptr || m_player->m_grapple->m_actor == nullptr) return false;
		if (h.grapple_d_size != sizeof(m_player->m_grapple->d)) return false;
		if (h.grapple_actor_d_size != sizeof(m_player->m_grapple->m_actor->d)) return false;
	}
	else
	{
		if (h.grapple_d_size != 0 || h.grapple_actor_d_size != 0) return false;
	}

	const std::size_t need = sizeof(savestate_header) + h.player_d_size + h.actor_d_size
		+ h.grapple_d_size + h.grapple_actor_d_size + h.rg_state_size;
	if (in_size < need) return false;

	std::size_t off = sizeof(savestate_header);
	std::memcpy(&m_player->d, in + off, h.player_d_size); off += h.player_d_size;
	std::memcpy(&m_player->m_actor->d, in + off, h.actor_d_size); off += h.actor_d_size;
	if (has_grapple)
	{
		std::memcpy(&m_player->m_grapple->d, in + off, h.grapple_d_size); off += h.grapple_d_size;
		std::memcpy(&m_player->m_grapple->m_actor->d, in + off, h.grapple_actor_d_size); off += h.grapple_actor_d_size;
	}
	std::memcpy(&m_rg_state, in + off, h.rg_state_size); off += h.rg_state_size;

	// memcpy bypassed actor::set_position so the cached aabb bounds + the
	// player's hitboxes (standing/sliding) are stale. Force-resync both.
	m_player->m_actor->d.position_changed = true;
	m_player->update_hitboxes();

	// Restore sim time so any timer comparisons remain consistent across
	// the save boundary. Other state fields (collision_engine, level) are
	// freshly spawned by the preceding load_*() call.
	m_state.m_time = emu::timespan{ static_cast<std::uint64_t>(h.state_time_ticks) };
	return true;
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

	// Time-mode arm-time savestate is captured before the sim has run at
	// all, so it reflects "spawned mid-air" rather than "settled on the
	// floor". After a few waiting frames the in-game player has fallen the
	// last few pixels onto the ground; if we replayed from the original
	// arm-time savestate the validator's player would still be mid-air at
	// tick 0 and miss every jump. Recapture the savestate the very tick
	// the player first presses anything — before this frame's sim step —
	// so client and server start the replay from byte-identical state.
	if (m_run_recorder.active && m_run_recorder.waiting_for_input
		&& input_bitmask != 0)
	{
		m_run_recorder.waiting_for_input = false;
		m_run_recorder.savestate.assign(64 * 1024, 0u);
		const std::size_t n = capture_savestate(
			m_run_recorder.savestate.data(),
			m_run_recorder.savestate.size());
		if (n > 0)
		{
			m_run_recorder.savestate_size = n;
			m_run_recorder.savestate.resize(n);
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

	// --- Per-run recorder (challenge modes) ---
	// Each run is its own short input log starting at tick 0 of the run,
	// paired with a savestate captured at first_tick. JS drains finished
	// runs via sr_run_consume_finished, which also re-arms the recorder
	// in place: a fresh savestate is captured at the current frame and
	// the next run begins.
	//
	// Run-end triggers (per game mode):
	//   grapple_challenge — grounded-and-not-swinging for
	//     k_ground_grace_ticks consecutive ticks (after airborne).
	//   rg_challenge      — RG counter goes from >0 to 0, OR ground touch
	//     (after airborne).
	// (waiting_for_input gate is cleared above, before m_state.update,
	//  so the same frame's sim step matches the validator's tick 0.)

	if ((m_game_mode == emu::GameMode::grapple_challenge ||
		 m_game_mode == emu::GameMode::rg_challenge ||
		 m_game_mode == emu::GameMode::time_challenge)
		&& m_player != nullptr && m_player->m_actor != nullptr
		&& m_run_recorder.active
		&& !m_run_recorder.waiting_for_input)
	{
		auto& rec = m_run_recorder;
		rec.global_tick++;

		const bool is_on_ground = m_player->d.is_on_ground;
		const float speed = m_player->m_actor->d.velocity.length();
		const int rg_consecutive = m_rg_state.consecutive;
		const int rg_session_best = m_rg_state.session_best;

		if (rec.first_tick)
		{
			// arm_recorder already captured the savestate against the
			// pre-sim player state. Now seed the log with the bitmask
			// that drove this first sim step (delta=0).
			rec.first_tick = false;
			rec.last_bitmask = input_bitmask;
			rec.last_event_global_tick = rec.global_tick;
			rec.max_speed = speed;
			if (rg_session_best > rec.max_streak) rec.max_streak = rg_session_best;
			rec.append_event(input_bitmask);
			// Refresh airborne gates from the post-sim state so a savestate
			// restored mid-air doesn't immediately satisfy a ground trigger.
			if (is_on_ground) rec.has_been_grounded = true;
			rec.was_on_ground_prev = is_on_ground;
			rec.was_on_ground_prev_rg = is_on_ground;
			rec.prev_rg_consecutive = rg_consecutive;
			rec.ground_streak_start_tick = 0;
		}

		if (speed > rec.max_speed) rec.max_speed = speed;
		if (rg_session_best > rec.max_streak) rec.max_streak = rg_session_best;

		if (input_bitmask != rec.last_bitmask)
		{
			rec.append_event(input_bitmask);
			rec.last_bitmask = input_bitmask;
		}

		// Airborne gate: ignore the spawn / restored-mid-air state until
		// the player has touched the ground at least once, then watch for
		// the next airborne transition. Without this a savestate captured
		// while airborne would immediately satisfy a ground-touch trigger
		// on the very next tick.
		if (is_on_ground) rec.has_been_grounded = true;
		if (rec.has_been_grounded && !is_on_ground) rec.has_been_airborne = true;

		bool finish_now = false;

		if (m_game_mode == emu::GameMode::grapple_challenge)
		{
			// Speed run-end: 0.25s of grounded-and-not-swinging after airborne.
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
				rec.end_tick = rec.ground_streak_start_tick;
				finish_now = true;
			}
		}
		else if (m_game_mode == emu::GameMode::rg_challenge)
		{
			// RG run-end: streak counter just dropped to 0, OR player just
			// touched the ground (rising edge). Both conditions require
			// has_been_airborne so the spawn / restored-mid-air state
			// doesn't immediately satisfy either trigger.
			const bool counter_dropped =
				rec.prev_rg_consecutive > 0 && rg_consecutive == 0;
			const bool ground_just_touched =
				is_on_ground && !rec.was_on_ground_prev_rg;

			if (rec.has_been_airborne && (counter_dropped || ground_just_touched))
			{
				rec.end_tick = rec.global_tick;
				finish_now = true;
			}
		}
		else // time_challenge
		{
			// Recording boundaries match the timer exactly:
			// start = first input (waiting_for_input gate, above), and
			// end = right edge crosses the goal line. Nothing else.
			const float right_edge =
				m_player->m_actor->d.position.x + m_player->m_actor->d.size.x;
			if (right_edge >= emu::time_challenge::end_x_threshold)
			{
				rec.end_tick = rec.global_tick;
				finish_now = true;
			}
		}

		// Hard cap on per-run payload — drop the recorder rather than let
		// a runaway state-machine grow the log unbounded.
		if (rec.log.size() > run_recorder::k_log_max_bytes)
		{
			rec.active = false;
		}

		rec.was_on_ground_prev = is_on_ground;
		rec.was_on_ground_prev_rg = is_on_ground;
		rec.prev_rg_consecutive = rg_consecutive;

		if (finish_now)
		{
			rec.finished = true;
			// Don't re-arm here — JS reads the run via sr_run_consume_finished
			// which captures a fresh savestate at the consume point and
			// resets the per-run buffers atomically.
		}
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

	// Optional motion-reference grid behind the world. RG-only because the
	// procedurally-generated corridor is the only mode where the lack of
	// landmarks makes vertical/horizontal motion hard to read at speed.
	// Bounds match load_rg_challenge's generate_corridor(... ceil_y=2,
	// floor_y=23 ...): corridor interior runs from y=(ceil_y+1)*16 = 48
	// (top of inside) to y=floor_y*16 = 368 (bottom of inside). Lines
	// sitting on the wall edges read as major.
	if (m_game_mode == emu::GameMode::rg_challenge && draw::visuals().show_rg_grid)
		draw::draw_rg_grid(m_camera, 48.0f, 368.0f);

	// Time-challenge goal marker: gold band on the inside face of the
	// right wall. Drawn before the world pass so the wall tile renders on
	// top, and the band only catches the eye when the player gets close
	// enough for the right wall to enter the viewport. Same y-bounds as
	// the RG grid because the corridor interior is identical.
	if (m_game_mode == emu::GameMode::time_challenge)
	{
		// Right wall left edge at (width - 1) * 16. Highlight the last
		// 96 wu of corridor (6 tiles) so the goal line reads at speed.
		constexpr float kRightWallX =
			(emu::time_challenge::corridor_width_tiles - 1) * 16.0f;
		draw::draw_time_goal(m_camera, kRightWallX - 96.0f, kRightWallX,
			48.0f, 368.0f);
	}

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
