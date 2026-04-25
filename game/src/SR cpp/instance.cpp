#include <algorithm>
#include <iostream>
#include <thread>

#include "instance.h"
#include "command/command_functions.h"

instance::instance() :
	m_input_handler{},
	m_inputs{ *m_input_handler.m_inputs },
	m_ups{ 0 },
	m_drawing_enabled{ false }
{
	auto now = std::chrono::high_resolution_clock::now();
	m_update_start = now;
	m_last_ups_t = now;
}

void instance::init()
{
	glfwInit();
	glewInit();

	enable_drawing(true);

	start_command_loop();

	add_command("getups", cmd::cmd_get_ups);
	add_command("enabledrawing", cmd::cmd_enable_drawing);
	add_command("newlevel", cmd::cmd_new_level);
	add_command("loadlevel", cmd::cmd_load_level);
	add_command("preplevel", cmd::cmd_prep_level);
	add_command("getvel", cmd::cmd_get_vel);
	add_command("showrightpotmap", cmd::cmd_show_right_pot_map);
	add_command("showleftpotmap", cmd::cmd_show_left_pot_map);
	add_command("printevents", cmd::cmd_print_events);
}

void instance::run()
{
	m_playground.init();

	while (true)
	{
		auto now = std::chrono::high_resolution_clock::now();
		emu::timespan delta = (now - m_update_start).count() / 100ull;
		m_update_start = now;

		while (now - m_last_ups_t >= std::chrono::seconds(1))
		{
			m_last_ups_t += std::chrono::seconds(1);
			m_ups = m_updates;
			m_updates = 0;
		}

		update_input();
		update(delta);
		draw();

		if (m_drawing_enabled)
			limit_rate(300);

		m_updates++;
	}
}

void instance::update(emu::timespan delta)
{
	if (m_win != nullptr && glfwWindowShouldClose(m_win))
		enable_drawing(false);

	{
		std::lock_guard<std::mutex> lock(m_command_queue_mtx);
		while (!m_command_queue.empty())
		{
			std::string_view command = m_command_queue.front();
			std::string_view command_name = cmd::extract_part(command);
			std::string command_name_lower;
			std::ranges::transform(command_name, std::back_inserter(command_name_lower), to_lower);
			auto it = m_commands.find(command_name_lower);
			if (it == m_commands.end())
			{
				std::cout << "ERROR: Unknown command \"" << command_name << "\"!\n";
				m_command_queue.erase(m_command_queue.begin());
				continue;
			}

			it->second->execute(*this, command);
			m_command_queue.erase(m_command_queue.begin());
		}
	}

	int width = 0;
	int height = 0;

	if (m_drawing_enabled)
	{
		glfwGetWindowSize(m_win, &width, &height);
		glViewport(0, 0, width, height);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, width, height, 0, -1, 1);
	}
	
	m_playground.update(delta, m_inputs, emu::vector{ (float)width, (float)height });
}

void instance::update_input()
{
	if (!m_drawing_enabled)
		return;

	m_input_handler.m_inputs->pressed_keys.reset();
	m_input_handler.m_inputs->pressed_buttons.reset();
	glfwPollEvents();

	m_playground.update_input(m_inputs);
}

void instance::draw()
{
	if (!m_drawing_enabled)
		return;

	glClear(GL_COLOR_BUFFER_BIT);
	
	m_playground.draw(m_inputs);

	glfwSwapBuffers(m_win);
}

void instance::limit_rate(std::uint64_t updates_per_sec) const
{
	while (
		std::chrono::high_resolution_clock::now() - m_update_start <
		std::chrono::nanoseconds(1000000000ull / updates_per_sec)) 
	{}
}

void instance::enable_drawing(bool enable)
{
	if (enable == m_drawing_enabled)
		return;

	m_drawing_enabled = enable;

	if (enable)
	{
		m_win = glfwCreateWindow((int)window_width, (int)window_height, "SR Bot", NULL, NULL);

		glfwMakeContextCurrent(m_win);

		glfwSwapInterval(0);

		glMatrixMode(GL_PROJECTION);
		glOrtho(0, window_width, window_height, 0, -1, 1);
		glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

		m_input_handler.init_callbacks(m_win);
	}
	else
	{
		glfwDestroyWindow(m_win);
		m_win = nullptr;
	}
}

void instance::start_command_loop()
{
	std::thread([this]()
		{
			std::string input;
			while (true)
			{
				std::getline(std::cin, input);
				std::cin.clear();

				std::lock_guard<std::mutex> lock(m_command_queue_mtx);
				m_command_queue.push_back(input);
			}
		}).detach();
}
