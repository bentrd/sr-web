#ifndef PLAYGROUND_H
#define PLAYGROUND_H

#define INIT_LOAD_LEVEL "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SpeedRunners\\Content\\Levels\\Multiplayer\\swiftpeaks.xnb"

#include "emulation/state.h"
#include "emulation/input.h"
#include "drawing/camera.h"
#include "input_handler.h"
#include "utility/event.h"
#include "utility/level_preprocessing.h"

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

	playground();

	void init();
	void reset();

	void update_input(const inputs& inputs);
	void update(emu::timespan delta, const inputs& inputs, emu::vector viewport_size);
	void draw(const inputs& inputs);
};

#endif
