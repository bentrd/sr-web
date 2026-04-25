#ifndef COMMAND_FUNCTIONS_H
#define COMMAND_FUNCTIONS_H

#include <string>

struct instance;

namespace cmd
{
	void cmd_get_ups(instance& inst);
	void cmd_enable_drawing(instance& inst, bool enable);
	void cmd_new_level(instance& inst, std::int32_t width, std::int32_t height);
	void cmd_load_level(instance& inst, std::string name);
	void cmd_prep_level(instance& inst);
	void cmd_get_vel(instance& inst);
	void cmd_show_right_pot_map(instance& inst, bool enable);
	void cmd_show_left_pot_map(instance& inst, bool enable);
	void cmd_print_events(instance& inst, bool enable);
}

#endif
