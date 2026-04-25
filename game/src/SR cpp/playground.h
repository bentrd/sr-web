#ifndef PLAYGROUND_H
#define PLAYGROUND_H

#include <string>

#include "emulation/state.h"
#include "emulation/input.h"
#include "drawing/camera.h"
#include "input_handler.h"
#include "utility/event.h"
#include "utility/level_preprocessing.h"
#include "network/ghost_manager.h"
#include "network/local_identity.h"

constexpr std::array<int, emu::input_count> input_map
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

	playground();

	void init();
	void load(const std::string& map_path);
	void reset();

	void update_input(const inputs& inputs);
	void update(emu::timespan delta, const inputs& inputs, emu::vector viewport_size);
	void draw(const inputs& inputs);
};

#endif
