#ifndef PLAYGROUND_H
#define PLAYGROUND_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "emulation/state.h"
#include "emulation/input.h"
#include "drawing/camera.h"
#include "emulation/game_mode.h"
#include "input_handler.h"
#include "utility/event.h"
#include "utility/level_preprocessing.h"
#include "network/ghost_manager.h"
#include "network/local_identity.h"

// Records a continuous input log for server-side replay validation in
// grapple_challenge mode. Recording starts at level-load (or manual reset)
// and runs without interruption until the next reset (or 256 KB overflow).
// On each floor-touch-after-airborne the `finished` flag is raised so JS
// can sample the current state and submit if it's a new PR — the recorder
// keeps running so subsequent attempts can also submit.
//
// Wire format:
//   [varint tickDelta][uint8 bitmask] [varint tickDelta][uint8 bitmask] ...
// The first entry has tickDelta=0 and carries the initial input state at
// recording start. Subsequent entries are emitted whenever the input
// bitmask changes. The server replays the deterministic sim from a freshly
// constructed level-state with this stream and compares max_speed.
struct run_recorder
{
	// Bumped whenever physics, input mapping, or mode parameters change in a
	// way that would invalidate previously-recorded streams. Server rejects
	// replays whose sim_version does not match the running server build.
	static constexpr std::uint32_t k_sim_version = 1;
	// Hard cap on log payload. ~4 hours of continuous play before reset is
	// required, given empirically-measured ~0.06 bytes/tick at 300 Hz.
	static constexpr std::size_t k_log_max_bytes = 256 * 1024;
	// Run-end requires the player to be on the ground continuously for
	// this many ticks, with no swing/grapple state during the streak. At
	// 300 Hz this is ~0.5s of "settled landing". The grace prevents
	// brief floor-grazes during a swing from cutting the run short.
	static constexpr int k_ground_grace_ticks = 150;

	// Monotonic sim-tick counter. Incremented on every sim step (1/300s),
	// independent of wall clock. Used as the time base for log tick deltas.
	std::uint64_t global_tick = 0;

	// True while we're recording (always true in challenge mode unless the
	// log has overflowed and is awaiting a reset).
	bool active = false;
	// True after a floor-touch-after-airborne event, until JS samples it.
	// Cleared by JS-side consume call; recorder keeps running.
	bool finished = false;
	// Have we left the ground at least once since the recording started or
	// the last `finished` event was raised? Gate for the next `finished`.
	bool has_been_airborne = false;
	// First sim tick of an active recording — seeds the log with the
	// initial bitmask at delta=0 so the replay knows the starting state.
	bool first_tick = true;

	// Edge detection — last sim step's grounded state. Retained for
	// compatibility with code that touches it during reset; the trigger
	// itself uses `ground_streak_start_tick` below.
	bool was_on_ground_prev = true;

	// First sim tick of the current "settled on ground, not swinging"
	// streak, or 0 when the player is airborne or in a swing/grapple
	// state. The run-end fires when the streak has lasted
	// k_ground_grace_ticks; end_tick is set to this value so the replay
	// terminates at the moment of landing rather than after the grace.
	std::uint64_t ground_streak_start_tick = 0;

	// Recording-scoped state.
	std::uint64_t start_tick = 0;       // global_tick when recording started
	std::uint64_t end_tick = 0;         // most recent floor-touch tick
	// global_tick of the floor touch BEFORE end_tick (i.e. the start of
	// the most recently completed attempt). 0 when no prior floor touch.
	// Threaded through to the replay so playback skips earlier attempts
	// in the same continuous recording and shows only the PR run.
	std::uint64_t prev_run_end_tick = 0;
	float max_speed = 0.0f;             // peak |velocity| across recording
	std::uint8_t last_bitmask = 0;
	std::uint64_t last_event_global_tick = 0;
	std::vector<std::uint8_t> log;

	// Drop the recording entirely (level load / manual reset / overflow).
	void clear();

	// Append an input change to the log: varint(global_tick - last_event_global_tick)
	// followed by a single bitmask byte. Updates last_event_global_tick.
	void append_event(std::uint8_t bitmask);
};

// Drives playback of a previously-recorded input log inside the browser
// sim. Replaces the live keyboard / controller input read in
// playground::update() while active, so the player's trajectory exactly
// matches the recording (assuming the same starting state — challenge
// modes regenerate the corridor + reset to PlayerStart on start_replay).
struct replay_state
{
	bool is_active = false;
	std::vector<std::uint8_t> log;
	std::size_t log_pos = 0;
	std::uint64_t tick = 0;             // current replay tick (0-based)
	std::uint64_t duration_ticks = 0;   // stop after this many sim steps

	// Peek of the next-pending event (not yet committed). Committed into
	// current_bitmask in step() once `tick` reaches next_event_tick.
	std::uint64_t next_event_tick = 0;
	std::uint8_t next_event_bitmask = 0;
	bool have_event = false;

	// Bitmask driving the current sim step. Updated in step() as events
	// commit. Constant between commits (i.e. inputs only "change" on the
	// recorded ticks).
	std::uint8_t current_bitmask = 0;

	// Mirror of the recorder's floor-touch detection. When the replayed
	// player has been airborne and lands on the ground, we deactivate the
	// replay so the user only sees the run that culminates in a floor
	// touch — not subsequent attempts that may exist in the same log.
	bool was_on_ground_prev = true;
	bool has_been_airborne = false;

	// Decode the next varint+byte from the log into next_event_tick /
	// next_event_bitmask, advancing log_pos. Returns false on EOF or
	// malformed varint (caller should mark have_event = false).
	bool read_next_event(std::uint64_t base_tick);

	// Drain all events scheduled for `tick` (updating current_bitmask)
	// and return the bitmask that should drive the upcoming sim step.
	std::uint8_t step();

	void clear();
};

// Mutable so JS can rebind individual actions via sr_set_binding().
// Defaults match the original SR-cpp scheme.
extern std::array<int, emu::input_count> input_map;

struct playground
{
	emu::level m_level;
	emu::state m_state;
	draw::camera m_camera;
	emu::player* m_player;

	bool m_draw_right_pot_map = false;
	bool m_draw_left_pot_map = false;
	bool m_print_events = false;

	bool m_paused = false;
	std::size_t m_step_count = 0;

	util::level_prep m_prep;
	util::get_event_helper m_helper;
	util::event_type m_last_event = util::evt_none;

	// Set from JS (sr_set_local_identity). When unset, the renderer
	// falls back to the original red.
	net::local_identity m_local_identity;
	net::ghost_manager m_ghosts;

	// Which game mode is active. Set by load*() / sr_load_* functions.
	emu::GameMode m_game_mode = emu::GameMode::standard;

	// Mode-specific state.
	// - grapple_challenge: m_session_max_speed
	// - rg_challenge:      m_rg_state
	emu::RgChallengeState m_rg_state;

	// Peak velocity magnitude (wu/s) recorded this session. Reset by
	// reset() and never clamped — the display side rounds to int.
	float m_session_max_speed = 0.0f;

	// Run recording for server-side replay anti-cheat. Only active in
	// grapple_challenge mode. See run_recorder above for the wire format.
	run_recorder m_run_recorder;

	// Browser-side playback of a previously-recorded run. While active,
	// the keyboard/controller input read is bypassed and the recorded
	// bitmask drives the sim instead. Recording is paused while playing
	// back so a replayed run can't itself be re-submitted as a PR.
	replay_state m_replay;

	// Per-frame controller input state pushed by JS via
	// sr_push_controller_input(). Merged (OR'd) with keyboard state
	// in update(). Reset at the end of tick_frame() so stale bits
	// never persist across frames.
	std::bitset<emu::input_count> m_controller_inputs{};

	// Reset controller bits. Called at the end of tick_frame() after
	// all sim steps have consumed this frame's input.
	void reset_controller_inputs();

	playground();

	void init();
	void load(const std::string& map_path);
	void load_challenge();
	// Load the same procedural corridor as load_challenge() but in RG mode.
	void load_rg_challenge();
	// Reset RG Challenge streak and detection state.
	void reset_rg_state();
	void reset();

	void update_input(const inputs& inputs);
	void update(emu::timespan delta, const inputs& inputs, emu::vector viewport_size);
	void draw(const inputs& inputs);

	// Start playing back a recorded run. mode 0 = grapple_challenge,
	// mode 1 = rg_challenge. Regenerates the corresponding procedural
	// corridor, resets the player to PlayerStart, and arms the replay
	// driver. `skip_ticks` synchronously fast-forwards the sim through
	// that many ticks of replay-driven input before returning, so
	// chained-attempt sessions can resume at the start of the actual PR
	// run rather than replaying every prior failure. Returns false on
	// malformed log, unsupported mode, or `skip_ticks >= duration_ticks`.
	bool start_replay(const std::uint8_t* log, std::size_t len,
		std::uint64_t duration_ticks, int mode,
		std::uint64_t skip_ticks);
	void stop_replay();
};

#endif
