#include <filesystem>
#include <fstream>

#include "level.h"
#include "state.h"
#include "player.h"
#include "grapple.h"
#include "file_util.h"

using namespace emu;

const std::string* level_actor::get(std::string_view key) const
{
	auto it = std::ranges::find_if(fields, [&](const auto& p) { return p.first == key; });
	if (it == fields.end())
		return nullptr;
	return &it->second;
}

level::level() :
	m_tile_layer{ 0, 0 }
{
	m_actors.emplace_back(emu::vector{ 0.0f, 0.0f }, emu::vector{ 25.0f, 45.0f }, std::string("PlayerStart"));
}

level::level(int32_t width, int32_t height) :
	m_tile_layer{ width, height }
{
	m_actors.emplace_back(emu::vector{ 0.0f, 0.0f }, emu::vector{ 25.0f, 45.0f }, std::string("PlayerStart"));
}

level::level(const char* filename)
{
	std::stringstream stream = decode_gzip(filename);

	(void)read_int32(stream); // version
	int32_t actor_count = read_int32(stream);

	for (int32_t i = 0; i < actor_count; i++)
	{
		level_actor actor;
		actor.position.x = read_float(stream);
		actor.position.y = read_float(stream);
		actor.size.x = read_float(stream);
		actor.size.y = read_float(stream);
		actor.type = read_string(stream);
		int32_t field_count = read_int32(stream);
		for (int32_t j = 0; j < field_count; j++)
		{
			std::string key = read_string(stream);
			std::string value = read_string(stream);
			actor.fields.emplace_back(std::move(key), std::move(value));
		}
		m_actors.push_back(std::move(actor));
	}

	int32_t layer_count = read_int32(stream);

	for (int32_t i = 0; i < layer_count; i++)
	{
		std::string layer = read_string(stream);
		int32_t width = read_int32(stream);
		int32_t height = read_int32(stream);
		if (layer != "Collision")
			stream.ignore(static_cast<std::streamsize>(width * height * 4));
		else
		{
			m_tile_layer = tile_layer_base{ width, height };
			for (int32_t x = 0; x < width; x++)
			{
				for (int32_t y = 0; y < height; y++)
				{
					m_tile_layer.set_tile(x, y, (tile_id)read_int32(stream));
				}
			}
		}
	}
}

const level_actor& level::get_actor(std::string_view type, int32_t index) const
{
	for (const level_actor& actor : m_actors)
	{
		if (actor.type == type)
		{
			if (index == 0)
				return actor;
			index--;
		}
	}

	throw std::invalid_argument{ "Actor not found!" };
}

void level::generate_corridor(level& lvl, int32_t width, int32_t height, int32_t ceil_y, int32_t floor_y, int32_t start_x)
{
	lvl = level{ width, height };

	for (int32_t x = 0; x < width; x++)
	{
		if (ceil_y >= 0 && ceil_y < height)
			lvl.m_tile_layer.set_tile(x, ceil_y, tile_grapple_ceil);
		if (floor_y >= 0 && floor_y < height)
			lvl.m_tile_layer.set_tile(x, floor_y, tile_square);
	}

	// Solid walls at the left and right ends so players can't fall off.
	// Span the full level height (above ceiling + below floor) so the
	// player can't escape vertically either, and make the walls 5 tiles
	// thick so high-speed runs (no_speed_cap) can't tunnel through.
	constexpr int32_t k_wall_thickness = 5;
	for (int32_t y = 0; y < height; y++)
	{
		for (int32_t k = 0; k < k_wall_thickness; k++)
		{
			if (k < width)
				lvl.m_tile_layer.set_tile(k, y, tile_square);
			const int32_t rx = width - 1 - k;
			if (rx >= 0)
				lvl.m_tile_layer.set_tile(rx, y, tile_square);
		}
	}

	// Player sits centered vertically in the air gap.
	const float spawn_y = static_cast<float>((ceil_y + 1 + floor_y - 1) / 2) * 16.0f;
	lvl.m_actors.clear();
	lvl.m_actors.emplace_back(
		emu::vector{ static_cast<float>(start_x) * 16.0f, spawn_y },
		emu::vector{ 25.0f, 45.0f },
		std::string("PlayerStart"));
}
