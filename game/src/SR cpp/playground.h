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

// Records a per-run input log + a starting savestate for server-side
// replay validation in grapple_challenge / rg_challenge mode. Each run
// gets its own log starting at tick 0 of the run and a binary blob
// capturing the player's full physics state at the recorder arm point —
// so the server can deterministically replay from arbitrary mid-session
// state, not only from a freshly-loaded level.
//
// Wire format (input log):
//   [varint tickDelta][uint8 bitmask] [varint tickDelta][uint8 bitmask] ...
// First entry has tickDelta=0 and carries the initial input bitmask.
// Subsequent entries emit on bitmask change. Bit layout: 0=left, 1=right,
// 2=jump, 3=grapple, 4=slide, 5=boost, 6=item, 7=swap.
//
// Run-end heuristics (per game mode):
//   grapple_challenge — player on ground (not swinging/grappling) for
//     k_ground_grace_ticks consecutive ticks after first being airborne.
//   rg_challenge      — RG counter goes from >0 to 0, OR player touches
//     the ground (after first being airborne).
//
// On each finish the C++ side commits the run (sets `finished`), JS reads
// the log + savestate via sr_run_consume_finished, then the recorder
// re-arms in place: a new savestate is captured at the current frame,
// the log is reset, and the next run begins.
struct run_recorder
{
	// Bumped whenever physics, input mapping, savestate format, or run-end
	// heuristics change in a way that would invalidate previously-recorded
	// streams. Server rejects replays whose sim_version does not match.
	static constexpr std::uint32_t k_sim_version = 2;
	// Hard cap on per-run log size. ~4 hours of continuous flight at the
	// empirically-measured ~0.06 bytes/tick — vastly more than any real
	// run, so this only fires on broken state machines.
	static constexpr std::size_t k_log_max_bytes = 256 * 1024;
	// Speed-mode run-end: grounded-and-not-swinging for this many
	// consecutive sim ticks (300 Hz → 75 = 0.25 s). Brief floor-grazes
	// mid-swing don't reset the streak because grapple/swing state is
	// excluded from the streak entirely.
	static constexpr int k_ground_grace_ticks = 75;

	// Monotonic sim-tick counter. Incremented every sim step (1/300s).
	// Resets to 0 on each new recording arm so per-run logs anchor at 0.
	std::uint64_t global_tick = 0;

	// True while we're recording. Cleared on level-load / manual reset /
	// overflow / replay playback.
	bool active = false;
	// Set when the recorder hits its run-end trigger and a fresh log is
	// ready to be drained by JS via sr_run_consume_finished. Cleared by
	// the consume call, which also re-arms the recorder for the next run.
	bool finished = false;
	// Gate so the spawn-fall landing doesn't trigger a finish immediately:
	// the player must touch the ground at least once before the next
	// airborne→ground transition counts.
	bool has_been_grounded = false;
	// Set the moment the player leaves the ground after `has_been_grounded`.
	// Required before any run-end trigger fires.
	bool has_been_airborne = false;
	// First-tick sentinel: when true, the next update() seeds the log
	// with the starting bitmask at delta=0 and captures the savestate.
	bool first_tick = true;
	// Time-challenge "press any key to start" gate. arm_recorder sets
	// this true for time_challenge so the timer doesn't tick until the
	// player issues input. Always false for speed/RG modes — they begin
	// counting from the moment the recorder arms.
	bool waiting_for_input = false;
	// Time-challenge insta-finish guard: true once the player has been
	// observed left of the goal line during this recording. Replaces the
	// `has_been_airborne` gate, which incorrectly required the player to
	// jump at least once before the goal touch could end the run — a flat
	// corridor lets a runner reach the goal without ever leaving the
	// ground.
	bool has_been_left_of_goal = false;

	// Edge detection helper for has_been_airborne.
	bool was_on_ground_prev = true;
	// Edge detection helper for the RG ground-touch trigger.
	bool was_on_ground_prev_rg = true;
	// Previous tick's RG consecutive counter — used to detect counter→0
	// transitions in rg_challenge mode.
	int prev_rg_consecutive = 0;

	// First sim tick of the current "grounded and not swinging" streak,
	// or 0 if the player is airborne or in a swing/grapple state. Used
	// only by the speed-mode run-end heuristic.
	std::uint64_t ground_streak_start_tick = 0;

	// Recording-scoped state. start_tick is always 0 in the new per-run
	// model (kept as a field so existing accessors compile unchanged).
	std::uint64_t start_tick = 0;
	std::uint64_t end_tick = 0;        // tick the run ended on (run-end trigger)
	float max_speed = 0.0f;            // peak |velocity| across this run
	int max_streak = 0;                // peak RG consecutive across this run
	std::uint8_t last_bitmask = 0;
	std::uint64_t last_event_global_tick = 0;
	std::vector<std::uint8_t> log;

	// Player physics savestate captured at first_tick. Sent to the server
	// alongside the input log so the validator restores the player to the
	// exact mid-session state the run started in. Format is opaque to the
	// recorder — produced/consumed by playground::capture_savestate /
	// playground::restore_savestate. Sized at the savestate header bytes
	// + the maximum POD payload; the actual bytes used is tracked in
	// `savestate_size`.
	std::vector<std::uint8_t> savestate;
	std::size_t savestate_size = 0;

	// Wipe everything. Called on level-load, manual reset, replay start,
	// and overflow. The recorder is re-armed from playground after this.
	void clear();

	// Append an input change to the log: varint(global_tick - last_event_global_tick)
	// followed by a single bitmask byte. Updates last_event_global_tick.
	void append_event(std::uint8_t bitmask);
};

// Savestate magic + current version. Increment savestate_version when
// any captured field's layout / size changes. The server-side validator
// rejects any savestate whose magic / version / size fields don't match
// the locally-built constants.
inline constexpr std::uint32_t k_savestate_magic = 0x56415350u; // 'PSAV' little-endian
inline constexpr std::uint32_t k_savestate_version = 1;

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
	// Load a fixed-length 30,000 wu corridor with the player anchored at
	// the left wall. Run-end is the player's right edge crossing the
	// goal line (the right wall) after at least one airborne→ground
	// transition. The recorder's duration_ticks IS the run's time.
	void load_time_challenge();
	// Reset RG Challenge streak and detection state.
	void reset_rg_state();
	void reset();

	void update_input(const inputs& inputs);
	void update(emu::timespan delta, const inputs& inputs, emu::vector viewport_size);
	void draw(const inputs& inputs);

	// Start playing back a recorded run. mode 0 = grapple_challenge,
	// mode 1 = rg_challenge, mode 2 = time_challenge. Regenerates the
	// corresponding procedural corridor, then restores the player from
	// `savestate` (so playback starts in the exact mid-session pose the
	// run was recorded against, not at PlayerStart). Returns false on
	// malformed log, unsupported mode, or savestate that fails validation.
	bool start_replay(const std::uint8_t* log, std::size_t len,
		std::uint64_t duration_ticks, int mode,
		const std::uint8_t* savestate, std::size_t savestate_len);
	void stop_replay();

	// Serialize the local player's current physics state into `out` and
	// return the number of bytes written, or 0 on error (no player /
	// out_size too small). Format: see k_savestate_magic at the top of
	// this header. Idempotent — captures everything needed for
	// restore_savestate to reproduce the exact next-tick behaviour.
	std::size_t capture_savestate(std::uint8_t* out, std::size_t out_size);

	// Apply a savestate captured by capture_savestate to the local player.
	// Returns true on success, false on magic / version / size mismatch.
	// The level itself is not touched — call load_*() first to lay down
	// the deterministic level state, then restore_savestate().
	bool restore_savestate(const std::uint8_t* in, std::size_t in_size);

	// Arm the run recorder for a fresh run starting from the current
	// player state. Captures a savestate, clears per-run accumulators,
	// and sets the recorder active. Called from load_*(), reset(), and
	// from sr_run_consume_finished after draining a finished run.
	// Returns true on success, false if the savestate capture failed
	// (in which case the recorder is left inactive).
	bool arm_recorder();
};

#endif
