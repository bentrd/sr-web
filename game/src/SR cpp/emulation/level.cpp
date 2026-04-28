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

void level::generate_corridor(level& lvl, int32_t width, int32_t height, int32_t ceil_y, int32_t floor_y, int32_t start_x, bool spawn_on_ground)
{
	lvl = level{ width, height };

	for (int32_t x = 0; x < width; x++)
	{
		if (ceil_y >= 0 && ceil_y < height)
			lvl.m_tile_layer.set_tile(x, ceil_y, tile_grapple_ceil);
		if (floor_y >= 0 && floor_y < height)
			lvl.m_tile_layer.set_tile(x, floor_y, tile_square);
	}

	// Single-tile walls at the left and right ends so players can't fall
	// off the sides. Spans the air gap between ceiling and floor so it
	// reads as a clean continuation of the corridor outline.
	const int32_t wall_top = std::max(0, ceil_y);
	const int32_t wall_bot = std::min(height - 1, floor_y);
	for (int32_t y = wall_top; y <= wall_bot; y++)
	{
		lvl.m_tile_layer.set_tile(0, y, tile_square);
		lvl.m_tile_layer.set_tile(width - 1, y, tile_square);
	}

	// Player sits centered vertically in the air gap by default. When
	// spawn_on_ground is true (time_challenge), place the player so its
	// feet rest on the floor instead — avoids a fall-in animation before
	// the run can start.
	constexpr float k_player_height_wu = 45.0f;
	const float spawn_y = spawn_on_ground
		? static_cast<float>(floor_y) * 16.0f - k_player_height_wu
		: static_cast<float>((ceil_y + 1 + floor_y - 1) / 2) * 16.0f;
	lvl.m_actors.clear();
	lvl.m_actors.emplace_back(
		emu::vector{ static_cast<float>(start_x) * 16.0f, spawn_y },
		emu::vector{ 25.0f, 45.0f },
		std::string("PlayerStart"));
}
