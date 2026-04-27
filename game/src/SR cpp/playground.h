#ifndef PLAYGROUND_H
#define PLAYGROUND_H

#include <string>

#include "emulation/state.h"
#include "emulation/input.h"
#include "drawing/camera.h"
#include "emulation/game_mode.h"
#include "input_handler.h"
#include "utility/event.h"
#include "utility/level_preprocessing.h"
#include "network/ghost_manager.h"
#include "network/local_identity.h"

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
};

#endif
