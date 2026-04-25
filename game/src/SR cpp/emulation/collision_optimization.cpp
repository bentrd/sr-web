#include "player.h"
#include "level.h"

using namespace emu;

static bool is_colliding_hori(vector p, float length, const level& level)
{
	const tile_layer_base& tile_layer = level.m_tile_layer;

	int32_t x = (int32_t)(p.x / 16.0f);
	int32_t y = (int32_t)(p.y / 16.0f);

	if (y < 0 || y >= tile_layer.m_height) [[unlikely]]
		return false;

	int32_t max_x = (int32_t)((p.x + length) / 16.0f);

	p.x -= x * 16.0f;
	p.y -= y * 16.0f;

	int32_t index = x + tile_layer.m_width * y;

	while (x <= max_x)
	{
		int32_t id = x >= 0 && x < tile_layer.m_width ?
			tile_layer.m_tilemap[index] :
			tile_air; [[unlikely]]
		switch (id)
		{
		case tile_air:
			break;
		case tile_square:
		case tile_checkered:
		case tile_grapple_ceil:
		case tile_wall_left:
		case tile_wall_right:
			return true;
		case tile_slope_floor_left:
		case tile_stairs_left:
		case tile_checkered_slope_floor_left:
			if (p.y >= p.x)
				return true;
			else
				break;
		case tile_slope_floor_right:
		case tile_stairs_right:
		case tile_checkered_slope_floor_right:
			if (p.x + length + p.y >= 16.0f)
				return true;
			else
				break;
		case tile_slope_ceil_left:
		case tile_checkered_slope_ceil_left:
			if (p.x + p.y <= 16.0f)
				return true;
			else
				break;
		case tile_slope_ceil_right:
		case tile_checkered_slope_ceil_right:
			if (p.x + length >= p.y)
				return true;
			else
				break;
		default:
			break;
		}

		float diff = 16.0f - p.x;
		p.x = 0.0f;
		length -= diff;

		x++;
		index++;
	}
	return false;
}

static bool is_colliding_vert(vector p, float length, const level& level)
{
	const tile_layer_base& tile_layer = level.m_tile_layer;

	int32_t x = (int32_t)(p.x / 16.0f);
	int32_t y = (int32_t)(p.y / 16.0f);

	if (x < 0 || x >= tile_layer.m_width) [[unlikely]]
		return false;

	int32_t max_y = (int32_t)((p.y + length) / 16.0f);

	p.x -= x * 16.0f;
	p.y -= y * 16.0f;

	int32_t index = x + tile_layer.m_width * y;

	while (y <= max_y)
	{
		int32_t id = y >= 0 && y < tile_layer.m_height ?
			tile_layer.m_tilemap[index] :
			tile_air; [[unlikely]]
		switch (id)
		{
		case tile_air:
			break;
		case tile_square:
		case tile_checkered:
		case tile_grapple_ceil:
		case tile_wall_left:
		case tile_wall_right:
			return true;
		case tile_slope_floor_left:
		case tile_stairs_left:
		case tile_checkered_slope_floor_left:
			if (p.y + length >= p.x)
				return true;
			else
				break;
		case tile_slope_floor_right:
		case tile_stairs_right:
		case tile_checkered_slope_floor_right:
			if (p.x + p.y + length >= 16.0f)
				return true;
			else
				break;
		case tile_slope_ceil_left:
		case tile_checkered_slope_ceil_left:
			if (p.x + p.y <= 16.0f)
				return true;
			else
				break;
		case tile_slope_ceil_right:
		case tile_checkered_slope_ceil_right:
			if (p.x >= p.y)
				return true;
			else
				break;
		default:
			break;
		}

		float diff = 16.0f - p.y;
		p.y = 0;
		length -= diff;

		y++;
		index += tile_layer.m_width;
	}
	return false;
}

bool level::is_colliding(const aabb& bounds, float ext) const
{
	vector pos_ul = { bounds.min_x - ext, bounds.min_y - ext };
	vector pos_ur = { bounds.max_x + ext, bounds.min_y - ext };
	vector pos_dl = { bounds.min_x - ext, bounds.max_y + ext };
		
	float d = 2.0f * ext;

	float width = bounds.width + d;
	float height = bounds.height + d;

	return
		::is_colliding_hori(pos_ul, width, *this) ||
		::is_colliding_vert(pos_ul, height, *this) ||
		::is_colliding_hori(pos_dl, width, *this) ||
		::is_colliding_vert(pos_ur, height, *this);
}

bool level::is_colliding_hori(vector start, float length, float ext) const
{
	vector pos_ul = { start.x - ext, start.y - ext };
	vector pos_ur = { start.x + length + ext, start.y - ext };
	vector pos_dl = { start.x - ext, start.y + ext };

	float height = ext + ext;
	float width = length + height;

	return
		::is_colliding_hori(pos_ul, width, *this) ||
		::is_colliding_vert(pos_ul, height, *this) ||
		::is_colliding_hori(pos_dl, width, *this) ||
		::is_colliding_vert(pos_ur, height, *this);
}

bool level::is_colliding_vert(vector start, float length, float ext) const
{
	vector pos_ul = { start.x - ext, start.y - ext };
	vector pos_ur = { start.x + ext, start.y - ext };
	vector pos_dl = { start.x - ext, start.y + length + ext };

	float width = ext + ext;
	float height = length + width;

	return
		::is_colliding_hori(pos_ul, width, *this) ||
		::is_colliding_vert(pos_ul, height, *this) ||
		::is_colliding_hori(pos_dl, width, *this) ||
		::is_colliding_vert(pos_ur, height, *this);
}

