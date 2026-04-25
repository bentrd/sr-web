#ifndef INSTANCE_H
#define INSTANCE_H

#include <unordered_map>
#include <mutex>
#include <chrono>

#include "input_handler.h"
#include "playground.h"
#include "command/command.h"
#include "emulation/timespan.h"
#include "emulation/level.h"

constexpr float window_width = 1280;
constexpr float window_height = 720;

constexpr emu::timespan delta = 33333;

struct instance
{
	GLFWwindow* m_win;

	input_handler m_input_handler;
	const inputs& m_inputs;

	std::vector<std::string> m_command_queue;
	std::mutex m_command_queue_mtx;
	std::unordered_map<std::string_view, std::unique_ptr<cmd::command_base>> m_commands;

	std::chrono::high_resolution_clock::time_point m_update_start;
	std::chrono::high_resolution_clock::time_point m_last_ups_t;
	std::size_t m_updates;
	std::size_t m_ups;
	bool m_drawing_enabled;

	playground m_playground;

	instance();

	void init();
	void run();

	void update(emu::timespan delta);
	void update_input();
	void draw();
	void limit_rate(std::uint64_t updates_per_sec) const;

	void enable_drawing(bool enable);

	void start_command_loop();

	template <typename... Ts>
	void add_command(std::string_view name, void(*func)(instance&, Ts...))
	{
		m_commands.emplace(name, new cmd::command<Ts...>(func));
	}
};

#endif
