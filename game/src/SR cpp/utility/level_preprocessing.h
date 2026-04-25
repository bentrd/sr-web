#ifndef LEVEL_PREPROCESSING_H
#define LEVEL_PREPROCESSING_H

#include <cstdint>
#include <memory>
#include <array>

#include "../emulation/tile_layer_base.h"
#include "../emulation/player.h"
#include "graph.h"

namespace emu
{
	struct state;
	struct level;
}

namespace util
{
	enum grap_pot : uint8_t
	{
		miss,
		hit
	};

	struct grap_pot_tile
	{
		std::uint16_t dist_left = 0;
		std::uint16_t dist_right = 0;
		grap_pot pot_left = miss;
		grap_pot pot_right = miss;
	};

	struct line
	{
		emu::vector pos;
		float length;
	};

	struct edge
	{
		std::size_t vertex;
		float weight;
	};

	struct vertex
	{
		emu::vector pos;

		inline float dist(const vertex& v) const
		{
			return (pos - v.pos).length();
		}

		inline vertex(emu::vector pos) :
			pos{ pos } {}
	};

	struct level_prep
	{
		const emu::level* m_level;

		std::unique_ptr<grap_pot_tile[]> m_right_pot_map;
		std::unique_ptr<grap_pot_tile[]> m_left_pot_map;

		// horizontal lines
		std::vector<line> m_floors;
		std::vector<line> m_ceils;
		// vertical lines
		std::vector<line> m_left_walls;
		std::vector<line> m_right_walls;
		// diagonal right lines
		std::vector<line> m_right_floor_slopes;
		std::vector<line> m_left_ceil_slopes;
		// diagonal left lines
		std::vector<line> m_left_floor_slopes;
		std::vector<line> m_right_ceil_slopes;

		graph<vertex> m_graph;
		std::vector<std::vector<path<vertex>>> m_paths;
		std::vector<std::vector<uint16_t>> m_reachable;
		mutable std::vector<uint16_t> m_oob_reachable1;
		mutable std::vector<uint16_t> m_oob_reachable2;

		level_prep() = default;
		level_prep(const emu::level& level);

	private:
		void refresh();

	public:
		path<vertex>& get_path(std::size_t vert1, std::size_t vert2);

		// finds the distance of the grapple ceiling from current player position if shooting a grapple actor diagonally upwards
		// returns FLOAT_MAX if the grapple misses
		float get_grap_dist(std::int32_t dir, const emu::player& player) const;

		// finds the shortest possible path from p1 to p2,
		// taking the maps collision and player's sliding hitbox into account
		std::pair<float, const path<vertex>*> get_min_path_dist(emu::vector p1, emu::vector p2) const;
	};
}

#endif
