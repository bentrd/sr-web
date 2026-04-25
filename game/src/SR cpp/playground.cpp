#include <random>
#include <iostream>
#include <chrono>

#include "playground.h"
#include "emulation/player.h"
#include "emulation/tile_actor.h"
#include "drawing/draw_util.h"

playground::playground()
{
	m_level = emu::level{ INIT_LOAD_LEVEL };
	//m_level = emu::level{ 2000, 2000 };
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

void playground::update_input(const inputs& inputs)
{
	int32_t sel_x = (int32_t)((inputs.cursor_x + m_camera.position.x) / 16.0f);
	int32_t sel_y = (int32_t)((inputs.cursor_y + m_camera.position.y) / 16.0f);

	emu::tile_id sel_tile = (emu::tile_id)((size_t)inputs.scroll_y % emu::tile_count);

	if (inputs.held_buttons[GLFW_MOUSE_BUTTON_LEFT] && m_level.m_tile_layer.m_tilemap != nullptr)
		m_level.m_tile_layer.set_tile(sel_x, sel_y, sel_tile);
	if (inputs.held_buttons[GLFW_MOUSE_BUTTON_RIGHT] && m_level.m_tile_layer.m_tilemap != nullptr)
		m_level.m_tile_layer.set_tile(sel_x, sel_y, emu::tile_air);
	if (inputs.held_keys['R'])
		reset();
	if (inputs.held_keys['J'] && m_player != nullptr)
	{
		std::cout << m_player->m_actor->d.velocity.x << " " << m_player->m_actor->d.velocity.y << " " << m_player->m_actor->d.position.x << " " << m_player->m_actor->d.position.y << "\n";
	}
	if (inputs.pressed_keys['F'])
	{
		m_paused = false;
		m_step_count = 1;
	}
	if (inputs.pressed_keys['G'])
	{
		m_paused = false;
		m_step_count = 10;
	}
	if (inputs.pressed_keys[GLFW_KEY_PAUSE])
	{
		m_paused = !m_paused;
		m_step_count = 0;
	}
}

std::mt19937 device(std::chrono::high_resolution_clock::now().time_since_epoch().count());

emu::vector p1;
emu::vector p2;

void playground::update(emu::timespan delta, const inputs& inputs, emu::vector viewport_size)
{

	if (inputs.pressed_buttons[GLFW_MOUSE_BUTTON_LEFT])
	{
		p1 = emu::vector{ (float)inputs.cursor_x, (float)inputs.cursor_y } + m_camera.position - emu::vector{ 12.5f, 12.5f };
	}
	if (inputs.pressed_buttons[GLFW_MOUSE_BUTTON_RIGHT])
	{
		p2 = emu::vector{ (float)inputs.cursor_x, (float)inputs.cursor_y } + m_camera.position - emu::vector{ 12.5f, 12.5f };
	}

	if (!m_paused)
	{
		for (size_t i = 0; i < m_state.m_inputs[0].size(); i++)
			m_state.m_inputs[0][i] = inputs.held_keys[input_map[i]];
	}

	if (!m_paused)
	{
		m_state.update(33333);
		m_camera.viewport_size = viewport_size;
		m_camera.update(33333, m_player->m_actor->d.position);

		util::event event = m_helper.get_event(*m_player);

		if (m_print_events && event.evt != m_last_event && event.evt != util::evt_none)
			std::cout << util::event::to_string(event.evt) << "\n";
		m_last_event = event.evt;
	}

	if (m_step_count > 0)
	{
		m_step_count--;
		if (m_step_count == 0)
			m_paused = true;
	}
}

void playground::draw(const inputs& inputs)
{
	if (m_draw_right_pot_map)
		draw::draw_right_pot_map(m_prep, m_camera);
	if (m_draw_left_pot_map)
		draw::draw_left_pot_map(m_prep, m_camera);

	draw::draw_state(&m_state, m_camera);

	std::int32_t sel_x = (int32_t)((inputs.cursor_x + m_camera.position.x) / 16.0f);
	std::int32_t sel_y = (int32_t)((inputs.cursor_y + m_camera.position.y) / 16.0f);

	emu::tile_id sel_tile = (emu::tile_id)((std::size_t)inputs.scroll_y % emu::tile_count);

	draw::draw_tile(sel_tile, emu::vector{ (float)(sel_x * 16), (float)(sel_y * 16) } - m_camera.position);

	auto path = m_prep.get_min_path_dist(p1, p2);

	if (path.second == nullptr || path.second->vertices.empty())
	{
		draw::draw_line(1.0f, 0.0f, 0.0f, p1 - m_camera.position + emu::vector{ 12.5f, 12.5f }, p2 - m_camera.position + emu::vector{ 12.5f, 12.5f });
		return;
	}

	draw::draw_line(1.0f, 0.0f, 0.0f, p1 - m_camera.position + emu::vector{ 12.5f, 12.5f }, path.second->vertices.front()->pos - m_camera.position + emu::vector{ 12.5f, 12.5f });
	for (size_t i = 0; i < path.second->vertices.size() - 1; i++)
	{
		draw::draw_line(1.0f, 0.0f, 0.0f, path.second->vertices[i]->pos - m_camera.position + emu::vector{ 12.5f, 12.5f }, path.second->vertices[i + 1]->pos - m_camera.position + emu::vector{ 12.5f, 12.5f });
	}
	draw::draw_line(1.0f, 0.0f, 0.0f, path.second->vertices.back()->pos - m_camera.position + emu::vector{ 12.5f, 12.5f }, p2 - m_camera.position + emu::vector{ 12.5f, 12.5f });
}
