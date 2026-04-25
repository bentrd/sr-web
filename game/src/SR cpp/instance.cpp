#include <algorithm>
#include <iostream>
#include <thread>

#include "instance.h"
#include "command/command_functions.h"
#include "drawing/draw_util.h"

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

	// Request a core 3.3 context up-front so the window is created with the
	// right profile. Forward-compat is required on macOS for any 3.x core.
	// Emscripten talks WebGL2 (GLES 3); the desktop hints don't apply and
	// glfwWindowHint(CONTEXT_VERSION) silently does nothing there.
#ifndef __EMSCRIPTEN__
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

	enable_drawing(true);

	// stdin/std::thread don't work in single-threaded WASM, and the web
	// build is driven entirely through sr_api anyway — there's no console.
#ifndef __EMSCRIPTEN__
	start_command_loop();
#endif

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

void instance::run(const std::string& map_path)
{
	m_playground.load(map_path);

	while (!should_close())
		tick_frame();
}

void instance::tick_frame()
{
	auto now = std::chrono::high_resolution_clock::now();
	emu::timespan real_delta = (now - m_update_start).count() / 100ull;
	m_update_start = now;

	while (now - m_last_ups_t >= std::chrono::seconds(1))
	{
		m_last_ups_t += std::chrono::seconds(1);
		m_ups = m_updates;
		m_updates = 0;
	}

	update_input();

	// Fixed-timestep accumulator: pump 1/300s sim steps until we've
	// caught up to wall-clock time. Cap at 8 steps per render frame
	// (~26ms of sim per frame) to avoid spiral-of-death after a tab
	// suspend / GC pause — better to drop time than freeze the page.
	m_sim_accumulator += real_delta;
	constexpr int max_steps_per_frame = 8;
	int steps = 0;
	while (m_sim_accumulator >= ::delta && steps < max_steps_per_frame)
	{
		update(::delta);
		m_sim_accumulator -= ::delta;
		++steps;
		m_updates++;
	}
	if (m_sim_accumulator > ::delta * max_steps_per_frame)
		m_sim_accumulator = ::delta * max_steps_per_frame;

	draw();

	// limit_rate is a busy-wait — only useful on desktop. The web build
	// is driven at monitor refresh by emscripten_set_main_loop_arg(0, ...).
#ifndef __EMSCRIPTEN__
	if (m_drawing_enabled)
		limit_rate(300);
#endif
}

bool instance::should_close() const
{
	// Original SR-cpp behavior is `while(true)` — closing the window flips
	// drawing off but the command loop keeps accepting stdin. We preserve
	// that here. The web target uses emscripten_set_main_loop (no while
	// loop at all), so this is desktop-only.
	return false;
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
		draw::set_viewport(width, height);
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

		// glewInit() requires a current GL context, so it lives here (not
		// in instance::init()) — and must run before any GL call.
#ifndef __EMSCRIPTEN__
		glewExperimental = GL_TRUE;
		glewInit();
#endif
		draw::init();
		draw::set_viewport((int)window_width, (int)window_height);
		// Dark gray play-area background — matches the lobby UI palette so
		// the canvas reads as part of the same app, and gives the white
		// stripes on grapple/wall tiles room to pop.
		glClearColor(0.16f, 0.17f, 0.20f, 1.0f);

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
