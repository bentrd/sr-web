#include <algorithm>

#include "draw_util.h"
#include "../emulation/tile_layer_base.h"

using namespace draw;
using namespace emu;

void draw::draw_triangle(float r, float g, float b, vector p1, vector p2, vector p3)
{
	glBegin(GL_TRIANGLES);
	glColor3f(r, g, b);
	glVertex2f(p1.x, p1.y);
	glVertex2f(p2.x, p2.y);
	glVertex2f(p3.x, p3.y);
	glEnd();
}

void draw::draw_rectangle(float r, float g, float b, const aabb& bounds)
{
	glBegin(GL_TRIANGLES);
	glColor3f(r, g, b);
	glVertex2f(bounds.min_x, bounds.min_y);
	glVertex2f(bounds.max_x, bounds.min_y);
	glVertex2f(bounds.max_x, bounds.max_x);
	glVertex2f(bounds.min_x, bounds.min_y);
	glVertex2f(bounds.min_x, bounds.max_y);
	glVertex2f(bounds.max_x, bounds.max_x);
	glEnd();
}

void draw::draw_rectangle(float r, float g, float b, vector p1, vector p2)
{
	glBegin(GL_TRIANGLES);
	glColor3f(r, g, b);
	glVertex2f(p1.x, p1.y);
	glVertex2f(p2.x, p1.y); 
	glVertex2f(p2.x, p2.y); 
	glVertex2f(p1.x, p1.y);
	glVertex2f(p1.x, p2.y);
	glVertex2f(p2.x, p2.y);
	glEnd();
}

void draw::draw_line(float r, float g, float b, vector p1, vector p2)
{
	glBegin(GL_LINES);
	glColor3f(r, g, b);
	glVertex2f(p1.x, p1.y);
	glVertex2f(p2.x, p2.y);
	glEnd();
}

void draw::draw_tile(emu::tile_id tile, vector pos)
{
	switch (tile)
	{
	case tile_air:
		break;
	case tile_square:
	case tile_checkered:
		draw_rectangle(
			0.0f, 0.0f, 0.0f,
			pos, pos + vector{ 16.0f, 16.0f });
		break;
	case tile_grapple_ceil:
		draw_rectangle(
			0.0f, 0.0f, 0.0f,
			pos, pos + vector{ 16.0f, 16.0f });
		draw_rectangle(
			0.0f, 0.0f, 1.0f,
			pos + vector{ 0.0f, 13.0f }, pos + vector{ 16.0f, 16.0f });
		break;
	case tile_wall_right:
		draw_rectangle(
			0.0f, 0.0f, 0.0f,
			pos, pos + vector{ 16.0f, 16.0f });
		draw_rectangle(
			0.0f, 1.0f, 0.0f,
			pos + vector{ 13.0f, 0.0f }, pos + vector{ 16.0f, 16.0f });
		break;
	case tile_wall_left:
		draw_rectangle(
			0.0f, 0.0f, 0.0f,
			pos, pos + vector{ 16.0f, 16.0f });
		draw_rectangle(
			0.0f, 1.0f, 0.0f,
			pos, pos + vector{ 3.0f, 16.0f });
		break;
	case tile_slope_floor_left:
	case tile_stairs_left:
	case tile_checkered_slope_floor_left:
		draw_triangle(
			0.0f, 0.0f, 0.0f,
			pos, pos + vector{ 0.0f, 16.0f }, pos + vector{ 16.0f, 16.0f });
		break;
	case tile_slope_floor_right:
	case tile_stairs_right:
	case tile_checkered_slope_floor_right:
		draw_triangle(
			0.0f, 0.0f, 0.0f,
			pos + vector{ 16.0f, 0.0f }, pos + vector{ 16.0f, 16.0f }, pos + vector{ 0.0f, 16.0f });
		break;
	case tile_slope_ceil_left:
	case tile_checkered_slope_ceil_left:
		draw_triangle(
			0.0f, 0.0f, 0.0f,
			pos, pos + vector{ 16.0f, 0.0f }, pos + vector{ 0.0f, 16.0f });
		break;
	case tile_slope_ceil_right:
	case tile_checkered_slope_ceil_right:
		draw_triangle(
			0.0f, 0.0f, 0.0f,
			pos, pos + vector{ 16.0f, 0.0f }, pos + vector{ 16.0f, 16.0f });
		break;
	}
}

void draw::draw_tile_layer(tile_layer_base* tile_layer, const camera& camera)
{
	std::int32_t x_init = std::clamp((std::int32_t)(camera.position.x / 16.0f) - 1, 0, (std::int32_t)tile_layer->m_width);
	std::int32_t x_end = std::clamp((std::int32_t)((camera.position.x + camera.viewport_size.x) / 16.0f) + 1, 0, (std::int32_t)tile_layer->m_width);

	std::int32_t y_init = std::clamp((std::int32_t)(camera.position.y / 16.0f) - 1, 0, (std::int32_t)tile_layer->m_height);
	std::int32_t y_end = std::clamp((std::int32_t)((camera.position.y + camera.viewport_size.y) / 16.0f) + 1, 0, (std::int32_t)tile_layer->m_height);

	for (std::int32_t y = y_init; y < y_end; y++)
	{
		for (std::int32_t x = x_init; x < x_end; x++)
		{
			vector screen_pos = vector{ (float)x, (float)y } * 16 - camera.position;

			draw::draw_tile(tile_layer->get_tile(x, y), screen_pos);
		}
	}
}

void draw::draw_player(player* player, const camera& camera)
{
	draw::draw_rectangle(
		1.0f, 0.0f, 0.0f, 
		player->get_collision()->get_vertex(0) - camera.position,
		player->get_collision()->get_vertex(2) - camera.position);

	vector top_left = { (camera.viewport_size.x - 200.0f) / 2, 15.0f };

	draw::draw_rectangle(0.7f, 0.7f, 0.7f, top_left, top_left + vector{ 200.0f, 25.0f });
	draw::draw_rectangle(0.0f, 0.0f, 1.0f, top_left, top_left + vector{ 200.0f * player->d.boost / 2.0f, 25.0f });
}

void draw::draw_grapple(grapple* grapple, const camera& camera)
{
	if (grapple->m_actor->d.is_collision_active)
	{
		draw::draw_rectangle(
			1.0f, 0.0f, 0.0f,
			grapple->m_actor->d.position - camera.position,
			grapple->m_actor->d.position + grapple->m_actor->d.size - camera.position);
		draw::draw_line(
			0.0f, 0.0f, 0.0f,
			grapple->get_center() - camera.position,
			grapple->m_owner->m_actor->get_collision()->get_center() - camera.position);
	}
}

void draw::draw_player_start(player_start* player_start, const camera& camera)
{

}

void draw::draw_super_boost_volume(super_boost_volume* super_boost_volume, const camera& camera)
{
	draw::draw_rectangle(
		0.0f, 1.0f, 0.0f,
		super_boost_volume->m_actor->m_bounds.get_vertex(0) - camera.position,
		super_boost_volume->m_actor->m_bounds.get_vertex(2) - camera.position);
}

void draw::draw_boost_section(boost_section* boost_section, const camera& camera)
{
	draw::draw_rectangle(
		0.0f, 1.0f, 0.0f,
		boost_section->m_actor->m_bounds.get_vertex(0) - camera.position,
		boost_section->m_actor->m_bounds.get_vertex(2) - camera.position);
}

void draw::draw_obstacle(obstacle* obstacle, const camera& camera)
{
	if (!obstacle->d.is_broken)
	{
		draw::draw_rectangle(
			1.0f, 0.5f, 0.0f,
			obstacle->m_actor->m_bounds.get_vertex(0) - camera.position,
			obstacle->m_actor->m_bounds.get_vertex(2) - camera.position);
	}
	else
	{
		draw::draw_rectangle(
			1.0f, 0.8f, 0.6f,
			obstacle->m_actor->m_bounds.get_vertex(0) - camera.position,
			obstacle->m_actor->m_bounds.get_vertex(2) - camera.position);
	}
}

void draw::draw_actor_controller(i_actor_controller* controller, const camera& camera)
{
	if (player* player = dynamic_cast<emu::player*>(controller))
		draw_player(player, camera);
	else if (grapple* grapple = dynamic_cast<emu::grapple*>(controller))
		draw_grapple(grapple, camera);
	else if (player_start* player_start = dynamic_cast<emu::player_start*>(controller))
		draw_player_start(player_start, camera);
	else if (super_boost_volume* super_boost_volume = dynamic_cast<emu::super_boost_volume*>(controller))
		draw_super_boost_volume(super_boost_volume, camera);
	else if (boost_section* boost_section = dynamic_cast<emu::boost_section*>(controller))
		draw_boost_section(boost_section, camera);
	else if (obstacle* obstacle = dynamic_cast<emu::obstacle*>(controller))
		draw_obstacle(obstacle, camera);
}

void draw::draw_state(state* state, const camera& camera)
{
	float x = 64.0f + std::floor(camera.position.x / 64.0f) * 64.0f - camera.position.x;
	while (x < camera.viewport_size.x)
	{
		draw::draw_line(0.8f, 0.8f, 0.8f, { x, 0.0f }, { x, camera.viewport_size.y });
		x += 64.0f;
	}

	float y = 64.0f + std::floor(camera.position.y / 64.0f) * 64.0f - camera.position.y;
	while (y < camera.viewport_size.y)
	{
		draw::draw_line(0.8f, 0.8f, 0.8f, { 0.0f, y }, { camera.viewport_size.x, y });
		y += 64.0f;
	}

	if (state->m_collision_engine.m_level != nullptr)
		draw_tile_layer(&(state->m_collision_engine.m_level->m_tile_layer), camera);
	for (auto& actor : state->actors())
		draw_actor_controller(actor->m_controller.get(), camera);
}

void draw::draw_right_pot_map(const util::level_prep& prep, const camera& camera)
{
	std::size_t width = prep.m_level->m_tile_layer.m_width;
	std::size_t height = prep.m_level->m_tile_layer.m_height;

	std::size_t x_init = std::clamp((std::int32_t)(camera.position.x / 16.0f) - 1, 0, (std::int32_t)width);
	std::size_t x_end = std::clamp((std::int32_t)((camera.position.x + camera.viewport_size.x) / 16.0f) + 1, 0, (std::int32_t)width);

	std::size_t y_init = std::clamp((std::int32_t)(camera.position.y / 16.0f) - 1, 0, (std::int32_t)height);
	std::size_t y_end = std::clamp((std::int32_t)((camera.position.y + camera.viewport_size.y) / 16.0f) + 1, 0, (std::int32_t)height);

	for (std::size_t y = y_init; y < y_end; y++)
	{
		for (std::size_t x = x_init; x < x_end; x++)
		{
			vector screen_pos = vector{ (float)x, (float)y } * 16 - camera.position;

			util::grap_pot_tile& pot = prep.m_right_pot_map[x + width * y];

			struct color
			{
				float r, g, b;
			};

			if (pot.pot_left != util::miss)
			{
				color col_l = color{ 0.0f, 1.0f - pot.dist_left / 50.0f, 0.0f };
				draw::draw_triangle(col_l.r, col_l.g, col_l.b, screen_pos, screen_pos + vector{ 16.0f, 0.0f }, screen_pos + vector{ 0.0f, 16.0f });
			}
			if (pot.pot_right != util::miss)
			{
				color col_r = color{ 0.0f, 1.0f - pot.dist_right / 50.0f, 0.0f };
				draw::draw_triangle(col_r.r, col_r.g, col_r.b, screen_pos + vector{ 16.0f, 0.0f }, screen_pos + vector{ 16.0f, 16.0f }, screen_pos + vector{ 0.0f, 16.0f });
			}
		}
	}
}

void draw::draw_left_pot_map(const util::level_prep& prep, const camera& camera)
{
	std::size_t width = prep.m_level->m_tile_layer.m_width;
	std::size_t height = prep.m_level->m_tile_layer.m_height;

	std::size_t x_init = std::clamp((std::int32_t)(camera.position.x / 16.0f) - 1, 0, (std::int32_t)width);
	std::size_t x_end = std::clamp((std::int32_t)((camera.position.x + camera.viewport_size.x) / 16.0f) + 1, 0, (std::int32_t)width);

	std::size_t y_init = std::clamp((std::int32_t)(camera.position.y / 16.0f) - 1, 0, (std::int32_t)height);
	std::size_t y_end = std::clamp((std::int32_t)((camera.position.y + camera.viewport_size.y) / 16.0f) + 1, 0, (std::int32_t)height);

	for (std::size_t y = y_init; y < y_end; y++)
	{
		for (std::size_t x = x_init; x < x_end; x++)
		{
			vector screen_pos = vector{ (float)x, (float)y } * 16 - camera.position;

			util::grap_pot_tile& pot = prep.m_left_pot_map[x + width * y];

			struct color
			{
				float r, g, b;
			};

			if (pot.pot_left != util::miss)
			{
				color col_l = color{ 0.0f, 1.0f - pot.dist_left / 50.0f, 0.0f };
				{
					draw::draw_triangle(col_l.r, col_l.g, col_l.b, screen_pos, screen_pos + vector{ 16.0f, 16.0f }, screen_pos + vector{ 0.0f, 16.0f });
				} 
			}
			if (pot.pot_right != util::miss)
			{
				color col_r = color{ 0.0f, 1.0f - pot.dist_right / 50.0f, 0.0f };
				draw::draw_triangle(col_r.r, col_r.g, col_r.b, screen_pos, screen_pos + vector{ 16.0f, 16.0f }, screen_pos + vector{ 16.0f, 0.0f });
			}
		}
	}
}
