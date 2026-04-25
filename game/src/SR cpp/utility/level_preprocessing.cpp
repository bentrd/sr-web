#include <queue>
#include <algorithm>
#include <iostream>
#include <thread>

#include "level_preprocessing.h"
#include "../emulation/state.h"
#include "../emulation/player.h"

using namespace emu;
using namespace util;

using sign_size_t = std::make_signed_t<std::size_t>;

// when splitting up a tile into two triangles (using either diagonal), 
// determines either the left or right triangle
enum triangle_side : std::uint8_t
{
	LEFT = 0,
	RIGHT = 1
};

constexpr collidable_type util_col_checkered = (collidable_type)9;

// converts a tile id to its corresponding collision type for more convenience
// to not have to differentiate between different looking tiles that behave the same
// 
// even though a checkered tile has col_square, we will treat it as having
// util_col_checkered
static std::int32_t to_col_type(std::int32_t tile_id)
{
	switch (tile_id)
	{
	case tile_air:
		return col_air;
	case tile_square:
		return col_square;
	case tile_grapple_ceil:
		return col_grapple_ceil;
	case tile_wall_left:
		return col_wall_left;
	case tile_wall_right:
		return col_wall_right;
	case tile_checkered:
		return util_col_checkered;
	case tile_slope_floor_right:
	case tile_stairs_right:
	case tile_checkered_slope_floor_right:
		return col_slope_floor_right;
	case tile_slope_floor_left:
	case tile_stairs_left:
	case tile_checkered_slope_floor_left:
		return col_slope_floor_left;
	case tile_slope_ceil_right:
	case tile_checkered_slope_ceil_right:
		return col_slope_ceil_right;
	case tile_slope_ceil_left:
	case tile_checkered_slope_ceil_left:
		return col_slope_ceil_left;
	default:
		return col_air;
	}
}

// traces a beam of triangles downwards left starting from the triangle specified by x, y and side,
// filling pot_map along its way until hitting an obstacle
static void trace_pot_left(const tile_layer_base& tile_layer, grap_pot_tile* pot_map, std::int32_t x, std::int32_t y, grap_pot pot, triangle_side side)
{
	std::uint16_t dist = side == triangle_side::LEFT ? 1 : 0;
	std::int32_t index = x + tile_layer.m_width * y;
	while (true)
	{
		std::int32_t col_type = to_col_type(tile_layer.m_tilemap[index]);

		if (side == triangle_side::LEFT)
		{
			if (col_type == col_air || col_type == util_col_checkered || col_type == col_slope_floor_right)
			{
				pot_map[index].dist_left = dist;
				pot_map[index].pot_left = pot;
				
				if (x == 0)
					return;

				x--;
				index--;

				side = triangle_side::RIGHT;
			}
			else
				return;
		}
		else
		{
			if (col_type == col_air || col_type == util_col_checkered || col_type == col_slope_ceil_left)
			{
				pot_map[index].dist_right = dist;
				pot_map[index].pot_right = pot;

				if (y == tile_layer.m_height - 1)
					return;

				y++;
				index += tile_layer.m_width;
				dist++;

				side = triangle_side::LEFT;
			}
			else
				return;
		}
	}
}

// generates a grapple potential map for right grapples
static std::unique_ptr<grap_pot_tile[]> gen_right_grap_pot_map(const tile_layer_base& tile_layer)
{
	std::int32_t size = tile_layer.m_width * tile_layer.m_height;
	std::unique_ptr<grap_pot_tile[]> output = std::make_unique<grap_pot_tile[]>(size);

	for (std::int32_t i = 0, x = 0, y = 0; i < size; i++)
	{
		std::int32_t col_above = y != 0 ? to_col_type(tile_layer.m_tilemap[static_cast<std::size_t>(i - tile_layer.m_width)]) : col_square;
		std::int32_t col_right = x != tile_layer.m_width - 1 ? to_col_type(tile_layer.m_tilemap[static_cast<std::size_t>(i + 1)]) : col_square;

		if (col_above == col_grapple_ceil)
			trace_pot_left(tile_layer, output.get(), x, y, hit, triangle_side::LEFT);
		else if (col_above != col_air && col_above != util_col_checkered && col_above != col_slope_ceil_left)
			trace_pot_left(tile_layer, output.get(), x, y, miss, triangle_side::LEFT);

		if (col_right == col_grapple_ceil)
			trace_pot_left(tile_layer, output.get(), x, y, hit, triangle_side::RIGHT);
		else if (col_right != col_air && col_right != util_col_checkered && col_above != col_slope_floor_right)
			trace_pot_left(tile_layer, output.get(), x, y, miss, triangle_side::RIGHT);

		x++;
		if (x == tile_layer.m_width)
		{
			x = 0;
			y++;
		}
	}

	return output;
}

// traces a beam of triangles downwards right starting from the triangle specified by x, y and side,
// filling pot_map along its way until hitting an obstacle
static void trace_pot_right(const tile_layer_base& tile_layer, grap_pot_tile* pot_map, std::int32_t x, std::int32_t y, grap_pot pot, triangle_side side)
{
	std::uint16_t dist = side == triangle_side::RIGHT ? 1 : 0;
	std::int32_t index = x + tile_layer.m_width * y;
	while (true)
	{
		std::int32_t col_type = to_col_type(tile_layer.m_tilemap[index]);

		if (side == triangle_side::RIGHT)
		{
			if (col_type == col_air || col_type == util_col_checkered || col_type == col_slope_floor_left)
			{
				pot_map[index].dist_right = dist;
				pot_map[index].pot_right = pot;

				if (x == tile_layer.m_width - 1)
					return;

				x++;
				index++;

				side = triangle_side::LEFT;
			}
			else
				return;
		}
		else
		{
			if (col_type == col_air || col_type == util_col_checkered || col_type == col_slope_ceil_right)
			{
				pot_map[index].dist_left = dist;
				pot_map[index].pot_left = pot;

				if (y == tile_layer.m_height - 1)
					return;

				y++;
				index += tile_layer.m_width;
				dist++;

				side = triangle_side::RIGHT;
			}
			else
				return;
		}
	}
}

// generates a grapple potential map for left grapples
static std::unique_ptr<grap_pot_tile[]> gen_left_grap_pot_map(const tile_layer_base& tile_layer)
{
	std::int32_t size = tile_layer.m_width * tile_layer.m_height;
	std::unique_ptr<grap_pot_tile[]> output = std::make_unique<grap_pot_tile[]>(size);

	for (std::int32_t i = 0, x = 0, y = 0; i < size; i++)
	{
		std::int32_t col_above = y != 0 ? to_col_type(tile_layer.m_tilemap[static_cast<std::size_t>(i - tile_layer.m_width)]) : col_square;
		std::int32_t col_left = x != 0 ? to_col_type(tile_layer.m_tilemap[static_cast<std::size_t>(i - 1)]) : col_square;

		if (col_above == col_grapple_ceil)
			trace_pot_right(tile_layer, output.get(), x, y, hit, triangle_side::RIGHT);
		else if (col_above != col_air && col_above != util_col_checkered && col_above != col_slope_ceil_right)
			trace_pot_right(tile_layer, output.get(), x, y, miss, triangle_side::RIGHT);

		if (col_left == col_grapple_ceil)
			trace_pot_right(tile_layer, output.get(), x, y, hit, triangle_side::LEFT);
		else if (col_left != col_air && col_left != util_col_checkered && col_left != col_slope_floor_left)
			trace_pot_right(tile_layer, output.get(), x, y, miss, triangle_side::LEFT);

		x++;
		if (x == tile_layer.m_width)
		{
			x = 0;
			y++;
		}
	}

	return output;
}


// finds the distance of the ceiling if tracing a given point diagonally upwards
// sets "hit" to true if the ceiling is a grapple ceiling
static float get_grap_dist_point(vector point, std::int32_t dir, bool& hit, std::int32_t width, std::int32_t height, grap_pot_tile* grap_pot_map)
{
	std::int32_t x_i = (int32_t)(point.x / 16.0f);
	std::int32_t y_i = (int32_t)(point.y / 16.0f);

	if (x_i < 0 || x_i >= width || y_i < 0 || y_i >= height) [[unlikely]]
		return FLOAT_MAX;

	const grap_pot_tile& tile = grap_pot_map[x_i + width * y_i];

	point.x -= x_i * 16.0f;
	point.y -= y_i * 16.0f;

	// check if point is inside left or right triangle of current tile
	bool right = dir == 1 ?
		point.x + point.y > 16.0f :
		point.x > point.y;

	if (right)
	{
		hit = tile.pot_right == grap_pot::hit;
		return tile.dist_right * 16.0f + point.y;
	}
	else
	{
		hit = tile.pot_left == grap_pot::hit;
		return tile.dist_left * 16.0f + point.y;
	}
}

// finds the distance of the grapple ceiling from current player position if shooting a grapple actor diagonally upwards
// returns FLOAT_MAX if the grapple misses
float level_prep::get_grap_dist(std::int32_t dir, const player& player) const
{
	grap_pot_tile* grap_pot_map = dir == 1 ?
		m_right_pot_map.get() :
		m_left_pot_map.get();

	const state& state = *player.m_actor->m_state;
	std::int32_t width = state.m_collision_engine.m_level->m_tile_layer.m_width;
	std::int32_t height = state.m_collision_engine.m_level->m_tile_layer.m_height;

	vector pos_ul = player.m_actor->d.position;
	if (dir == 1)
		pos_ul.x += 25.0f;

	vector pos_ur = vector{ pos_ul.x + 12.0f, pos_ul.y };
	vector pos_dl = vector{ pos_ul.x, pos_ul.y + 12.0f };
	vector pos_dr = vector{ pos_ul.x + 12.0f, pos_ul.y + 12.0f };

	// check all 4 corner points of the grapples hitbox
	// we really do need to check all 4 corners and can in no case exit prematurely

	bool hit = false;
	float dist = get_grap_dist_point(pos_ul, dir, hit, width, height, grap_pot_map);

	bool hit2 = false;
	float dist2 = get_grap_dist_point(pos_ur, dir, hit2, width, height, grap_pot_map);
	if (dist2 < dist)
	{
		dist = dist2;
		hit = hit2;
	}
	dist2 = get_grap_dist_point(pos_dl, dir, hit2, width, height, grap_pot_map);
	if (dist2 < dist)
	{
		dist = dist2;
		hit = hit2;
	}
	dist2 = get_grap_dist_point(pos_dr, dir, hit2, width, height, grap_pot_map);
	if (dist2 < dist)
	{
		dist = dist2;
		hit = hit2;
	}

	if (hit)
		return dist;
	else
		return FLOAT_MAX;
}

static bool has_corner_top_left(std::int32_t tile)
{
	switch (to_col_type(tile))
	{
	case col_square:
	case col_grapple_ceil:
	case util_col_checkered:
	case col_wall_left:
	case col_wall_right:
	case col_slope_floor_left:
	case col_slope_ceil_right:
	case col_slope_ceil_left:
		return true;
	default:
		return false;
	}
}

static bool has_corner_top_right(std::int32_t tile)
{
	switch (to_col_type(tile))
	{
	case col_square:
	case col_grapple_ceil:
	case util_col_checkered:
	case col_wall_left:
	case col_wall_right:
	case col_slope_floor_right:
	case col_slope_ceil_right:
	case col_slope_ceil_left:
		return true;
	default:
		return false;
	}
}

static bool has_corner_bottom_left(std::int32_t tile)
{
	switch (to_col_type(tile))
	{
	case col_square:
	case col_grapple_ceil:
	case util_col_checkered:
	case col_wall_left:
	case col_wall_right:
	case col_slope_floor_right:
	case col_slope_floor_left:
	case col_slope_ceil_left:
		return true;
	default:
		return false;
	}
}

static bool has_corner_bottom_right(std::int32_t tile)
{
	switch (to_col_type(tile))
	{
	case col_square:
	case col_grapple_ceil:
	case util_col_checkered:
	case col_wall_left:
	case col_wall_right:
	case col_slope_floor_right:
	case col_slope_floor_left:
	case col_slope_ceil_right:
		return true;
	default:
		return false;
	}
}

static bool has_edge_top(std::int32_t tile)
{
	switch (to_col_type(tile))
	{
	case col_square:
	case col_grapple_ceil:
	case util_col_checkered:
	case col_wall_left:
	case col_wall_right:
	case col_slope_ceil_right:
	case col_slope_ceil_left:
		return true;
	default:
		return false;
	}
}

static bool has_edge_bottom(std::int32_t tile)
{
	switch (to_col_type(tile))
	{
	case col_square:
	case col_grapple_ceil:
	case util_col_checkered:
	case col_wall_left:
	case col_wall_right:
	case col_slope_floor_right:
	case col_slope_floor_left:
		return true;
	default:
		return false;
	}
}

static bool has_edge_left(std::int32_t tile)
{
	switch (to_col_type(tile))
	{
	case col_square:
	case col_grapple_ceil:
	case util_col_checkered:
	case col_wall_left:
	case col_wall_right:
	case col_slope_floor_left:
	case col_slope_ceil_left:
		return true;
	default:
		return false;
	}
}

static bool has_edge_right(std::int32_t tile)
{
	switch (to_col_type(tile))
	{
	case col_square:
	case col_grapple_ceil:
	case util_col_checkered:
	case col_wall_left:
	case col_wall_right:
	case col_slope_floor_right:
	case col_slope_ceil_right:
		return true;
	default:
		return false;
	}
}

static bool hori_line_pred(const line& l1, const line& l2)
{
	return l1.pos.y < l2.pos.y;
}

template <bool Floor>
static std::vector<line> get_hori_lines(const tile_layer_base& tile_layer)
{
	std::vector<line> lines;
	bool is_line = false;
	line cur_line;
	for (std::int32_t i = 0, x = 0, y = 0; y < tile_layer.m_height + 1; i++)
	{
		std::int32_t id =
			y < tile_layer.m_height ?
			tile_layer.m_tilemap[i] :
			tile_air;
		std::int32_t id_above =
			y >= 1 ?
			tile_layer.m_tilemap[static_cast<std::size_t>(i - tile_layer.m_width)] :
			tile_air;

		if ((!Floor ^ has_edge_top(id)) && (Floor ^ has_edge_bottom(id_above)))
		{
			if (!is_line)
			{
				cur_line.pos = { x * 16.0f, y * 16.0f };
				cur_line.length = 16.0f;
				is_line = true;
			}
			else
				cur_line.length += 16.0f;
		}
		else if (is_line)
		{
			lines.push_back(cur_line);
			is_line = false;
		}

		x++;
		if (x == tile_layer.m_width)
		{
			x = 0;
			y++;
			if (is_line)
				lines.push_back(cur_line);
		}
	}

	std::sort(lines.begin(), lines.end(), hori_line_pred);

	return lines;
}

static bool vert_line_pred(const line& l1, const line& l2)
{
	return l1.pos.x < l2.pos.x;
}

template <bool Left>
static std::vector<line> get_vert_lines(const tile_layer_base& tile_layer)
{
	std::vector<line> lines;
	bool is_line = false;
	line cur_line;
	std::uint32_t size = tile_layer.m_width * tile_layer.m_height;
	for (std::int32_t i = 0, x = 0, y = 0; x < tile_layer.m_width + 1; i += tile_layer.m_width)
	{
		std::int32_t id =
			x < tile_layer.m_width ?
			tile_layer.m_tilemap[i] :
			tile_air;
		std::int32_t id_left =
			x >= 1 ?
			tile_layer.m_tilemap[static_cast<std::size_t>(i - 1)] :
			tile_air;

		if ((!Left ^ has_edge_left(id)) && (Left ^ has_edge_right(id_left)))
		{
			if (!is_line)
			{
				cur_line.pos = { x * 16.0f, y * 16.0f };
				cur_line.length = 16.0f;
				is_line = true;
			}
			else
				cur_line.length += 16.0f;
		}
		else if (is_line)
		{
			lines.push_back(cur_line);
			is_line = false;
		}

		y++;
		if (y == tile_layer.m_height)
		{
			y = 0;
			x++;
			i -= size;
			i++;
			if (is_line)
				lines.push_back(cur_line);
		}
	}

	std::sort(lines.begin(), lines.end(), vert_line_pred);

	return lines;
}

static bool diag_right_line_pred(const line& l1, const line& l2)
{
	return l1.pos.x - l1.pos.y < l2.pos.x - l2.pos.y;
}

template <std::int32_t Tile>
static std::vector<line> get_diag_right_lines(const tile_layer_base& tile_layer)
{
	std::vector<line> lines;
	bool is_line = false;
	line cur_line;
	std::uint32_t size = tile_layer.m_width * tile_layer.m_height;
	for (std::int32_t i = size - tile_layer.m_width, x = 0, y = tile_layer.m_height - 1;;)
	{
		std::int32_t id =
			x < tile_layer.m_width ?
			tile_layer.m_tilemap[i] :
			tile_air;
		
		if (to_col_type(id) == Tile)
		{
			if (!is_line)
			{
				cur_line.pos = { x * 16.0f, y * 16.0f };
				cur_line.length = 16.0f;
				is_line = true;
			}
			else
				cur_line.length += 16.0f;
		}
		else if (is_line)
		{
			lines.push_back(cur_line);
			is_line = false;
		}

		x++;
		y++;
		if (x == tile_layer.m_width || y == tile_layer.m_height)
		{
			x++;
			std::int32_t d = std::min(x, y);
			x -= d;
			y -= d;
			i = x + tile_layer.m_width * y;

			if (is_line)
				lines.push_back(cur_line);

			if (x >= tile_layer.m_width || y >= tile_layer.m_height)
				break;
		}
		else
			i += 1 + tile_layer.m_width;
	}

	std::sort(lines.begin(), lines.end(), diag_right_line_pred);

	return lines;
}

static bool diag_left_line_pred(const line& l1, const line& l2)
{
	return l1.pos.x + l1.pos.y < l2.pos.x + l2.pos.y;
}

template <std::int32_t Tile>
static std::vector<line> get_diag_left_lines(const tile_layer_base& tile_layer)
{
	std::vector<line> lines;
	bool is_line = false;
	line cur_line;
	for (std::int32_t i = 0, x = 0, y = 0;;)
	{
		std::int32_t id =
			x < tile_layer.m_width ?
			tile_layer.m_tilemap[i] :
			tile_air;

		if (to_col_type(id) == Tile)
		{
			if (!is_line)
			{
				cur_line.pos = { (x + 1) * 16.0f, y * 16.0f };
				cur_line.length = 16.0f;
				is_line = true;
			}
			else
				cur_line.length += 16.0f;
		}
		else if (is_line)
		{
			lines.push_back(cur_line);
			is_line = false;
		}

		x--;
		y++;
		if (x == -1 || y == tile_layer.m_height)
		{
			x++;
			std::int32_t d = std::min(tile_layer.m_width - x, y);
			x += d;
			y -= d;
			i = x + tile_layer.m_width * y;

			if (is_line)
				lines.push_back(cur_line);

			if (x >= tile_layer.m_width || y >= tile_layer.m_height)
				break;
		}
		else
			i += -1 + tile_layer.m_width;
	}

	std::sort(lines.begin(), lines.end(), diag_left_line_pred);

	return lines;
}

static std::vector<vertex> get_vertices(const level& level)
{
	const tile_layer_base& tile_layer = level.m_tile_layer;

	std::vector<vertex> vertices;

	for (std::int32_t i = 0, x = 0, y = 0; y < tile_layer.m_height + 1; i++)
	{
		std::int32_t id = 
			x < tile_layer.m_width && y < tile_layer.m_height ?
			tile_layer.m_tilemap[i] :
			tile_air;
		std::int32_t id_left =
			x >= 1 && y < tile_layer.m_height ?
			tile_layer.m_tilemap[static_cast<std::size_t>(i - 1)] :
			tile_air;
		std::int32_t id_above =
			x < tile_layer.m_width && y >= 1 ?
			tile_layer.m_tilemap[static_cast<std::size_t>(i - tile_layer.m_width)] :
			tile_air;
		std::int32_t id_above_left =
			x >= 1 && y >= 1 ?
			tile_layer.m_tilemap[static_cast<std::size_t>(i - 1 - tile_layer.m_width)] :
			tile_air;

		bool c_this = has_corner_top_left(id);
		bool c_left = has_corner_top_right(id_left);
		bool c_above = has_corner_bottom_left(id_above);
		bool c_above_left = has_corner_bottom_right(id_above_left);

		if (
			(c_this || c_left || c_above || c_above_left) &&
			!(c_this && c_above_left) && !(c_left && c_above)
			)
		{
			vector pos = { x * 16.0f, y * 16.0f };
			
			if (
				!has_edge_right(id_left) && !has_edge_bottom(id_above) &&
				!level.is_colliding(aabb{ pos.x, pos.x + 25.0f, pos.y, pos.y + 25.0f }, -1.0f))
				vertices.emplace_back(pos);
			if (
				!has_edge_left(id) && !has_edge_bottom(id_above_left) &&
				!level.is_colliding(aabb{ pos.x - 25.0f, pos.x, pos.y, pos.y + 25.0f }, -1.0f))
				vertices.emplace_back(pos - vector{ 25.0f, 0.0f });
			if (
				!has_edge_right(id_above_left) && !has_edge_top(id) &&
				!level.is_colliding(aabb{ pos.x, pos.x + 25.0f, pos.y - 25.0f, pos.y }, -1.0f))
				vertices.emplace_back(pos - vector{ 0.0f, 25.0f });
			if (
				!has_edge_left(id_above) && !has_edge_top(id_left) &&
				!level.is_colliding(aabb{ pos.x - 25.0f, pos.x, pos.y - 25.0f, pos.y }, -1.0f))
				vertices.emplace_back(pos - vector{ 25.0f, 25.0f });
		}

		x++;
		if (x == tile_layer.m_width + 1)
		{
			x = 0;
			y++;
			i--;
		}
	}

	return vertices;
}

static float cross(vector v1, vector v2)
{
	return v1.x * v2.y - v1.y * v2.x;
}

static bool hori_line_intersect(const line& hori_line, vector v1, vector v2)
{
	vector dv = v2 - v1;
	vector d1 = v1 - hori_line.pos;

	float num = cross(d1, dv);
	float denom = hori_line.length * dv.y;

	return
		0.0f < num && num < denom;
}

static bool hori_line_intersect(const std::vector<line>& hori_lines, vector v1, vector v2)
{
	if (v1.y > v2.y)
		std::swap(v1, v2);

	line dummy;

	dummy.pos.y = v1.y;
	auto it1 = std::upper_bound(hori_lines.begin(), hori_lines.end(), dummy, hori_line_pred);

	dummy.pos.y = v2.y;
	auto it2 = std::lower_bound(hori_lines.begin(), hori_lines.end(), dummy, hori_line_pred);

	for (; it1 < it2; ++it1)
	{
		if (hori_line_intersect(*it1, v1, v2))
			return true;
	}
	return false;
}

static bool vert_line_intersect(const line& vert_line, vector v1, vector v2)
{
	vector dv = v2 - v1;
	vector d1 = v1 - vert_line.pos;

	float num = cross(dv, d1);
	float denom = vert_line.length * dv.x;

	return
		0.0f < num && num < denom;
}

static bool vert_line_intersect(const std::vector<line>& vert_lines, vector v1, vector v2)
{
	if (v1.x > v2.x)
		std::swap(v1, v2);

	line dummy;

	dummy.pos.x = v1.x;
	auto it1 = std::upper_bound(vert_lines.begin(), vert_lines.end(), dummy, vert_line_pred);

	dummy.pos.x = v2.x;
	auto it2 = std::lower_bound(vert_lines.begin(), vert_lines.end(), dummy, vert_line_pred);

	for (; it1 < it2; ++it1)
	{
		if (vert_line_intersect(*it1, v1, v2))
			return true;
	}
	return false;
}

static bool diag_right_line_intersect(const line& diag_right_line, vector v1, vector v2)
{
	vector dv = v2 - v1;
	vector d1 = v1 - diag_right_line.pos;

	float num = cross(d1, dv);
	float denom = diag_right_line.length * (dv.y - dv.x);

	return
		0.0f > num && num > denom;
}

static bool diag_right_line_intersect(const std::vector<line>& diag_right_lines, vector v1, vector v2)
{
	if (v1.x - v1.y > v2.x - v2.y)
		std::swap(v1, v2);

	line dummy;

	dummy.pos = v1;
	auto it1 = std::upper_bound(diag_right_lines.begin(), diag_right_lines.end(), dummy, diag_right_line_pred);

	dummy.pos = v2;
	auto it2 = std::lower_bound(diag_right_lines.begin(), diag_right_lines.end(), dummy, diag_right_line_pred);

	for (; it1 < it2; ++it1)
	{
		if (diag_right_line_intersect(*it1, v1, v2))
			return true;
	}
	return false;
}

static bool diag_left_line_intersect(const line& diag_left_line, vector v1, vector v2)
{
	vector dl = vector(-diag_left_line.length, diag_left_line.length);
	vector dv = v2 - v1;
	vector d1 = v1 - diag_left_line.pos;

	float num = cross(d1, dv);
	float denom = -diag_left_line.length * (dv.x + dv.y);

	return
		0.0f > num && num > denom;
}

static bool diag_left_line_intersect(const std::vector<line>& diag_left_lines, vector v1, vector v2)
{
	if (v1.x + v1.y > v2.x + v2.y)
		std::swap(v1, v2);

	line dummy;

	dummy.pos = v1;
	auto it1 = std::upper_bound(diag_left_lines.begin(), diag_left_lines.end(), dummy, diag_left_line_pred);

	dummy.pos = v2;
	auto it2 = std::lower_bound(diag_left_lines.begin(), diag_left_lines.end(), dummy, diag_left_line_pred);

	for (; it1 < it2; ++it1)
	{
		if (diag_left_line_intersect(*it1, v1, v2))
			return true;
	}
	return false;
}

static bool any_line_intersect(const level_prep& prep, vector v1, vector v2)
{
	// check only the necessary lines, depending on the direction of d = v2 - v1
	// we only check lines where d can penetrate into the tilemaps collision
	// (and not we d goes out of its collision, like for ceilings when d goes downwards)

	if (v1.y > v2.y) // ensure v1.y <= v2.y, so we only test for downwards d
		std::swap(v1, v2);

	vector d = v2 - v1;

	if (d.x >= 0.0f) // d goes right
	{
		if (
			hori_line_intersect(prep.m_floors, v1, v2) ||
			vert_line_intersect(prep.m_left_walls, v1, v2) ||
			diag_left_line_intersect(prep.m_left_floor_slopes, v1, v2) 
			)
			return true;

		if (d.y >= d.x) // d is more steep
		{
			return
				diag_right_line_intersect(prep.m_right_floor_slopes, v1, v2);
		}
		else // d is more flat
		{
			return
				diag_right_line_intersect(prep.m_left_ceil_slopes, v1, v2);
		}
	}
	else // d goes left
	{
		if (
			hori_line_intersect(prep.m_floors, v1, v2) ||
			vert_line_intersect(prep.m_right_walls, v1, v2) ||
			diag_right_line_intersect(prep.m_right_floor_slopes, v1, v2)
			)
			return true;

		if (d.y >= -d.x) // d is more steep
		{
			return
				diag_left_line_intersect(prep.m_left_floor_slopes, v1, v2);
		}
		else // d is more flat
		{
			return
				diag_left_line_intersect(prep.m_right_ceil_slopes, v1, v2);
		}
	}

	// we actually never end up checking for ceiling lines, but
	// I'll still leave them in
}

static void get_test_points(vector v1, vector v2, std::array<vector, 3>& tp1, std::array<vector, 3>& tp2, float size1 = 25.0f, float size2 = 25.0f)
{
	constexpr float eps = 1.0f / 16.0f;

	if ((v1.x < v2.x) == (v1.y < v2.y))
	{
		tp1[0] = v1 + vector{ size1 - eps, eps };
		tp1[1] = v1 + vector{ eps, size1 - eps };
		tp2[0] = v2 + vector{ size2 - eps, eps };
		tp2[1] = v2 + vector{ eps, size2 - eps };
	}
	else
	{
		tp1[0] = v1 + vector{ eps, eps };
		tp1[1] = v1 + vector{ size1 - eps, size1 - eps };
		tp2[0] = v2 + vector{ eps, eps };
		tp2[1] = v2 + vector{ size2 - eps, size2 - eps };
	}

	constexpr float r1 = 15.0f / 32.0f;
	constexpr float r2 = 17.0f / 32.0f;

	tp1[2] = r1 * tp1[0] + r2 * tp1[1];
	tp2[2] = r1 * tp2[0] + r2 * tp2[1];
}

static bool any_line_intersect_box(const level_prep& prep, vector v1, vector v2, float size1 = 25.0f, float size2 = 25.0f)
{
	std::array<vector, 3> tp1;
	std::array<vector, 3> tp2;
	get_test_points(v1, v2, tp1, tp2, size1, size2);

	for (std::size_t k = 0; k < 3; k++)
	{
		if (any_line_intersect(prep, tp1[k], tp2[k]))
			return true;
	}

	return false;
}

static void find_edges(level_prep& prep)
{
	auto& vertices = prep.m_graph.vertices;

	if (vertices.empty())
		return;

	std::size_t size = vertices.size();
	for (std::size_t i = 0; i < size - 1; i++)
	{
		for (std::size_t j = i + 1; j < size; j++)
		{
			auto& vert1 = vertices[i];
			auto& vert2 = vertices[j];
			
			if (!any_line_intersect_box(prep, vert1.pos, vert2.pos))
			{
				float dist = vert1.dist(vert2);
				prep.m_graph.connect_bi(i, j, dist);
			}
		}
	}
}

static std::vector<std::uint16_t> find_reachable_tile(const level_prep& prep, std::int32_t x, std::int32_t y)
{
	std::vector<std::uint16_t> reachable;
	for (std::size_t j = 0; j < prep.m_graph.vertices.size(); j++)
	{
		vector tile_pos = vector{ x * 16.0f, y * 16.0f } + vector{ 3.5f, 3.5f };
		vector vert_pos = prep.m_graph.vertices[j].pos;

		if (!any_line_intersect_box(prep, tile_pos, vert_pos, 9.0f, 25.0f))
			reachable.push_back((std::uint16_t)j);
	}
	return reachable;
}

static void find_reachable_thread(level_prep& prep, std::size_t off, std::size_t stride)
{
	const level& level = *prep.m_level;
	std::size_t width = level.m_tile_layer.m_width;
	std::size_t height = level.m_tile_layer.m_height;

	auto& tile_layer = level.m_tile_layer;

	for (std::size_t i = off * width, x = 0, y = off; y < height; i++)
	{
		std::int32_t id = tile_layer.m_tilemap[i];

		if (id == tile_air)
		{
			prep.m_reachable[i] = find_reachable_tile(prep, (std::int32_t)x, (std::int32_t)y);
			prep.m_reachable[i].shrink_to_fit();
		}

		x++;
		if (x == width)
		{
			x = 0;
			y += stride;
			i += (stride - 1) * width;
		}
	}
}

static void find_reachable(level_prep& prep)
{
	const level& level = *prep.m_level;
	std::size_t width = level.m_tile_layer.m_width;
	std::size_t height = level.m_tile_layer.m_height;
	prep.m_reachable = std::vector<std::vector<std::uint16_t>>(width * height);

	std::array<std::thread, 8> threads;

	for (std::size_t i = 0; i < 8; i++)
	{
		threads[i] = std::thread{ find_reachable_thread, std::ref(prep), i, 8 };
	}
	for (std::size_t i = 0; i < 8; i++)
	{
		threads[i].join();
	}
}

// counts through each pair of natural number once,
// i.e. providing a bijection between |N and |N x |N
// it counts "uniformly", in a sense that it first counts through all pairs
// with numbers <= 0, then <= 1, <= 2, <= 3 and so on (without any pairs repeating though)
struct uniform_counter
{
	std::size_t i1 = 0;
	std::size_t i2 = 0;
	std::size_t i1_end = SIZE_MAX;
	std::size_t i2_end = SIZE_MAX;

private:
	void skip_all_i1()
	{
		if (i1 > i2)
			i2 = i1;
		else if (i1 <= i2)
			i2++;
		i1 = 0;
	}

	void skip_all_i2()
	{
		if (i1 > i2)
			i1++;
		else if (i1 <= i2)
			i1 = i2 + 1;
		i2 = 0;
	}

public:
	bool next()
	{
		if (i1 > i2)
		{
			i2++;
			if (i1 == i2)
			{
				i1 = 0;
			}
		}
		else if (i1 < i2)
			i1++;
		else if (i1 == i2)
		{
			i1++;
			i2 = 0;
		}

		if (i1 >= i1_end)
		{
			skip_all_i1();

			if (i2 >= i2_end)
				return false;
		}
		else if (i2 >= i2_end)
		{
			skip_all_i2();

			if (i1 >= i1_end)
				return false;
		}

		return true;
	}
};

struct reach_dist
{
	float dist;
	float dist_sum;
	std::uint16_t reach;
	bool intersect_tested;
	bool intersect;

	inline bool operator<(const reach_dist& right) const
	{
		return dist_sum < right.dist_sum;
	}
};

static void get_next_shortest(
	const level_prep& prep,
	std::vector<reach_dist>& reach_dist1, std::vector<reach_dist>& reach_dist2, 
	std::size_t& shortest_i1, std::size_t& shortest_i2, float& prev_shortest)
{
	float shortest_dist = INFINITY;

	uniform_counter uc;
	uc.i1_end = reach_dist1.size();
	uc.i2_end = reach_dist2.size();
	for (bool res = true; res; res = uc.next())
	{
		reach_dist& rd1 = reach_dist1[uc.i1];
		reach_dist& rd2 = reach_dist2[uc.i2];

		if (rd1.dist_sum >= shortest_dist)
		{
			uc.i1_end = uc.i1;
			continue;
		}

		if (rd2.dist_sum >= shortest_dist)
		{
			uc.i2_end = uc.i2;
			continue;
		}

		if (rd1.intersect_tested && rd1.intersect)
			continue;

		if (rd2.intersect_tested && rd2.intersect)
			continue;

		const path<vertex>& path = prep.m_paths[rd1.reach][rd2.reach];
		float dist = rd1.dist + path.dist + rd2.dist;

		if (dist >= shortest_dist || dist < prev_shortest)
			continue;
		
		shortest_dist = dist;
		shortest_i1 = uc.i1;
		shortest_i2 = uc.i2;
	}

	prev_shortest = shortest_dist;
}

std::pair<float, const path<vertex>*> level_prep::get_min_path_dist(vector p1, vector p2) const
{
	if (!any_line_intersect_box(*this, p1, p2))
		return { (p2 - p1).length(), nullptr };

	sign_size_t x1 = (sign_size_t)std::floor((p1.x + 12.5f) / 16.0f);
	sign_size_t y1 = (sign_size_t)std::floor((p1.y + 12.5f) / 16.0f);
	sign_size_t x2 = (sign_size_t)std::floor((p2.x + 12.5f) / 16.0f);
	sign_size_t y2 = (sign_size_t)std::floor((p2.y + 12.5f) / 16.0f);

	bool oob1 = x1 < 0 || x1 >= m_level->m_tile_layer.m_width || y1 < 0 || y1 >= m_level->m_tile_layer.m_height;
	bool oob2 = x2 < 0 || x2 >= m_level->m_tile_layer.m_width || y2 < 0 || y2 >= m_level->m_tile_layer.m_height;

	if (oob1) [[unlikely]]
		m_oob_reachable1 = find_reachable_tile(*this, (std::int32_t)x1, (std::int32_t)y1);
	if (oob2) [[unlikely]]
		m_oob_reachable2 = find_reachable_tile(*this, (std::int32_t)x2, (std::int32_t)y2);

	auto& reach1 = !oob1 ?
		m_reachable[x1 + m_level->m_tile_layer.m_width * y1] :
		m_oob_reachable1;
	auto& reach2 = !oob2 ?
		m_reachable[x2 + m_level->m_tile_layer.m_width * y2] :
		m_oob_reachable2;

	if (reach1.empty() || reach2.empty())
		return { INFINITY, nullptr };

	static thread_local std::vector<reach_dist> reach_dist1;
	static thread_local std::vector<reach_dist> reach_dist2;

	reach_dist1.clear();
	reach_dist1.resize(reach1.size());
	reach_dist2.clear();
	reach_dist2.resize(reach2.size());

	for (std::size_t i = 0; i < reach_dist1.size(); i++)
	{
		vector vert = m_graph.vertices[reach1[i]].pos;
		reach_dist1[i].reach = reach1[i];
		reach_dist1[i].dist = (vert - p1).length();
		reach_dist1[i].dist_sum = reach_dist1[i].dist + (p2 - vert).length();
		reach_dist1[i].intersect_tested = false;
	}
	std::sort(reach_dist1.begin(), reach_dist1.end());

	for (std::size_t i = 0; i < reach_dist2.size(); i++)
	{
		vector vert = m_graph.vertices[reach2[i]].pos;
		reach_dist2[i].reach = reach2[i];
		reach_dist2[i].dist = (p2 - vert).length();
		reach_dist2[i].dist_sum = reach_dist2[i].dist + (vert - p1).length();
		reach_dist2[i].intersect_tested = false;
	}
	std::sort(reach_dist2.begin(), reach_dist2.end());

	float shortest_dist = -INFINITY;
	std::size_t shortest_i1 = 0;
	std::size_t shortest_i2 = 0;

	while (true)
	{
		get_next_shortest(
			*this, 
			reach_dist1, reach_dist2, 
			shortest_i1, shortest_i2, shortest_dist);

		if (shortest_dist == INFINITY)
			return { INFINITY, nullptr };

		reach_dist& rd1 = reach_dist1[shortest_i1];
		reach_dist& rd2 = reach_dist2[shortest_i2];

		if (!rd1.intersect_tested)
		{
			rd1.intersect = any_line_intersect_box(*this, p1, m_graph.vertices[rd1.reach].pos);
			rd1.intersect_tested = true;
		}

		if (rd1.intersect)
			continue;

		if (!rd2.intersect_tested)
		{
			rd2.intersect = any_line_intersect_box(*this, m_graph.vertices[rd2.reach].pos, p2);
			rd2.intersect_tested = true;
		}

		if (rd2.intersect)
			continue;

		return { shortest_dist, &m_paths[reach_dist1[shortest_i1].reach][reach_dist2[shortest_i2].reach] };
	}
}

level_prep::level_prep(const emu::level& level) :
	m_level{ &level }
{
	refresh();
}

void level_prep::refresh()
{
	m_right_pot_map = gen_right_grap_pot_map(m_level->m_tile_layer);
	m_left_pot_map = gen_left_grap_pot_map(m_level->m_tile_layer);

	m_floors = get_hori_lines</*Floor = */true>(m_level->m_tile_layer);
	m_ceils = get_hori_lines</*Floor = */false>(m_level->m_tile_layer);
	m_left_walls = get_vert_lines</*Left = */true>(m_level->m_tile_layer);
	m_right_walls = get_vert_lines</*Left = */false>(m_level->m_tile_layer);
	m_right_floor_slopes = get_diag_right_lines<col_slope_floor_left>(m_level->m_tile_layer);
	m_left_ceil_slopes = get_diag_right_lines<col_slope_ceil_right>(m_level->m_tile_layer);
	m_left_floor_slopes = get_diag_left_lines<col_slope_floor_right>(m_level->m_tile_layer);
	m_right_ceil_slopes = get_diag_left_lines<col_slope_ceil_left>(m_level->m_tile_layer);

	auto vertices = get_vertices(*m_level);
	if (vertices.size() > 1ull << 16)
		std::cout << "WARNING: Only a max of 65536 corners are allowed per level!\n";
	m_graph = graph<vertex>(std::move(vertices));
	find_edges(*this);
	m_paths = m_graph.dijkstra_all();
	find_reachable(*this);
}

path<vertex>& level_prep::get_path(std::size_t vert1, std::size_t vert2)
{
	return m_paths[vert1][vert2];
}
