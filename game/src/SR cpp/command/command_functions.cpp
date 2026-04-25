#include <fstream>
#include <iostream>

#include "command_functions.h"
#include "../instance.h"
#include "../emulation/player.h"
#include "../emulation/player_start.h"
#include "../emulation/tile_layer_base.h"

using namespace cmd;

static std::string_view enabled_string(bool enable)
{
	return enable ? "Enabled" : "Disabled";
}

void cmd::cmd_get_ups(instance& inst)
{
	std::cout << inst.m_ups << "\n";
}

void cmd::cmd_enable_drawing(instance& inst, bool enable)
{
	inst.enable_drawing(enable);
	std::cout << std::format("{} drawing.\n", enabled_string(enable));
}

void cmd::cmd_new_level(instance& inst, std::int32_t width, std::int32_t height)
{
	inst.m_playground.m_level = emu::level{ width, height };
	inst.m_playground.init();
	std::cout << "Created a new level.\n";
}

void cmd::cmd_load_level(instance& inst, std::string name)
{
	inst.m_playground.m_level = emu::level{ name.c_str() };
	inst.m_playground.init();
	std::cout << std::format("Loaded level \"{}\"\n", name);
}

void cmd::cmd_prep_level(instance& inst)
{
	inst.m_playground.m_prep = util::level_prep{ inst.m_playground.m_level };
	std::cout << "Preprocessed level.\n";
}

void cmd::cmd_get_vel(instance& inst)
{
	emu::player* player = inst.m_playground.m_state.get_contr<emu::player>(0);
	if (player == nullptr)
		throw std::runtime_error{ "No player!" };
	std::cout << std::format("{} {}\n", player->m_actor->d.velocity.x, player->m_actor->d.velocity.y);
}

void cmd::cmd_show_right_pot_map(instance& inst, bool enable)
{
	inst.m_playground.m_draw_right_pot_map = enable;
	inst.m_playground.m_draw_left_pot_map &= !enable;
	std::cout << std::format("{} drawing right grapple potential map.\n", enabled_string(enable));
}

void cmd::cmd_show_left_pot_map(instance& inst, bool enable)
{
	inst.m_playground.m_draw_left_pot_map = enable;
	inst.m_playground.m_draw_right_pot_map &= !enable;
	std::cout << std::format("{} drawing left grapple potential map.\n", enabled_string(enable));
}

void cmd::cmd_print_events(instance& inst, bool enable)
{
	inst.m_playground.m_print_events = enable;
	std::cout << std::format("{} printing events.\n", enabled_string(enable));
}