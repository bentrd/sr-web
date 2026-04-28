#ifndef LEVEL_H
#define LEVEL_H

#include "tile_layer_base.h"

namespace emu
{
	struct level_actor
	{
		vector position;
		vector size;
		std::string type;
		std::vector<std::pair<std::string, std::string>> fields;

		const std::string* get(std::string_view key) const;
	};

	struct level
	{
		std::vector<level_actor> m_actors;
		tile_layer_base m_tile_layer;

		level();
		level(int32_t width, int32_t height);
		level(const char* filename);

		const level_actor& get_actor(std::string_view type, int32_t index = 0) const;

		bool is_colliding(const aabb& bounds, float ext = 1.0f) const;
		bool is_colliding_hori(vector start, float length, float ext = 1.0f) const;
		bool is_colliding_vert(vector start, float length, float ext = 1.0f) const;

		// spawn_on_ground=true places PlayerStart with feet on the floor instead
		// of centered in the air gap. Used by time_challenge so the run starts
		// without a "drop in" animation. Default false preserves the centered
		// spawn used by speed/RG modes.
		static void generate_corridor(level& lvl, int32_t width, int32_t height, int32_t ceil_y, int32_t floor_y, int32_t start_x, bool spawn_on_ground = false);
	};
}

#endif
