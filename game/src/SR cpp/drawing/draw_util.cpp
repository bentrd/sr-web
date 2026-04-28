#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "draw_util.h"
#include "visuals_config.h"
#include "../emulation/tile_layer_base.h"

using namespace draw;
using namespace emu;

// ---------------------------------------------------------------------------
// Modern-GL backend (batched)
//
// All public draw_* funnel into push_tri() / push_line(), which append
// interleaved (vec2 pos, vec4 rgba) vertices into per-mode CPU buffers.
// flush_frame() uploads each buffer once and issues exactly one
// glDrawArrays per primitive mode (triangles + lines). This collapses
// what used to be thousands of glDrawArrays per frame (one per tile,
// one per gridline, one per actor) into TWO per frame. The cost of all
// the JS<->WASM<->WebGL hops was the dominant frame-time bottleneck;
// batching turns it into pure GPU throughput.
//
// The shader accepts per-vertex color so every primitive can have its
// own (r,g,b,a) without breaking the batch. No texturing — every
// primitive in this game is a flat-shaded rect, line, or triangle.
//
// The original SR-cpp used immediate-mode (`glBegin`/`glVertex2f`),
// which WebGL doesn't support and macOS only honors in a compat profile.
// ---------------------------------------------------------------------------

namespace
{
#ifdef __EMSCRIPTEN__
	const char* k_vert_src =
		"#version 300 es\n"
		"precision highp float;\n"
		"layout(location = 0) in vec2 a_pos;\n"
		"layout(location = 1) in vec4 a_color;\n"
		"uniform mat4 u_proj;\n"
		"out vec4 v_color;\n"
		"void main() {\n"
		"  v_color = a_color;\n"
		"  gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
		"}\n";

	const char* k_frag_src =
		"#version 300 es\n"
		"precision highp float;\n"
		"in vec4 v_color;\n"
		"out vec4 o_color;\n"
		"void main() { o_color = v_color; }\n";
#else
	const char* k_vert_src =
		"#version 330 core\n"
		"layout(location = 0) in vec2 a_pos;\n"
		"layout(location = 1) in vec4 a_color;\n"
		"uniform mat4 u_proj;\n"
		"out vec4 v_color;\n"
		"void main() {\n"
		"  v_color = a_color;\n"
		"  gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
		"}\n";

	const char* k_frag_src =
		"#version 330 core\n"
		"in vec4 v_color;\n"
		"out vec4 o_color;\n"
		"void main() { o_color = v_color; }\n";
#endif

	// 6 floats per vertex: x, y, r, g, b, a.
	constexpr GLsizei k_vert_stride_floats = 6;

	struct gl_state
	{
		GLuint program = 0;
		GLuint vao = 0;
		GLuint vbo = 0;
		GLint u_proj = -1;
		GLsizei vbo_capacity_verts = 0;
		float proj[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		bool initialized = false;

		// Per-frame CPU vertex buffers. Cleared in flush_frame after upload.
		std::vector<float> tri_verts;
		std::vector<float> line_verts;

		// Local player color override (sr_set_local_identity → playground::draw).
		float local_r = 1.0f;
		float local_g = 0.0f;
		float local_b = 0.0f;
	};

	gl_state g_state;

	GLuint compile_shader(GLenum type, const char* src)
	{
		GLuint sh = glCreateShader(type);
		glShaderSource(sh, 1, &src, nullptr);
		glCompileShader(sh);

		GLint ok = GL_FALSE;
		glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
		if (!ok)
		{
			char log[1024] = {};
			glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
			std::fprintf(stderr, "draw: shader compile failed: %s\n", log);
			std::exit(1);
		}
		return sh;
	}

	GLuint link_program(GLuint vs, GLuint fs)
	{
		GLuint prog = glCreateProgram();
		glAttachShader(prog, vs);
		glAttachShader(prog, fs);
		glLinkProgram(prog);

		GLint ok = GL_FALSE;
		glGetProgramiv(prog, GL_LINK_STATUS, &ok);
		if (!ok)
		{
			char log[1024] = {};
			glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
			std::fprintf(stderr, "draw: program link failed: %s\n", log);
			std::exit(1);
		}
		return prog;
	}

	void make_ortho(float* out, float left, float right, float bottom, float top, float near_p, float far_p)
	{
		// Column-major. Maps (left..right) -> (-1..+1), (top..bottom) -> (-1..+1).
		// We pass top=0, bottom=h so (0..h) screen y maps to (-1..+1) NDC y
		// with top of screen at y=-1 (matches glOrtho(0,w,h,0,-1,1)).
		const float rl = right - left;
		const float tb = top - bottom;
		const float fn = far_p - near_p;

		out[0]  = 2.0f / rl;  out[1]  = 0.0f;        out[2]  = 0.0f;            out[3]  = 0.0f;
		out[4]  = 0.0f;       out[5]  = 2.0f / tb;   out[6]  = 0.0f;            out[7]  = 0.0f;
		out[8]  = 0.0f;       out[9]  = 0.0f;        out[10] = -2.0f / fn;      out[11] = 0.0f;
		out[12] = -(right + left) / rl;
		out[13] = -(top + bottom) / tb;
		out[14] = -(far_p + near_p) / fn;
		out[15] = 1.0f;
	}

	inline void push_vert(std::vector<float>& buf, float x, float y, float r, float g, float b, float a)
	{
		buf.push_back(x); buf.push_back(y);
		buf.push_back(r); buf.push_back(g); buf.push_back(b); buf.push_back(a);
	}

	inline void push_tri(float r, float g, float b, float a,
		float x1, float y1, float x2, float y2, float x3, float y3)
	{
		auto& v = g_state.tri_verts;
		push_vert(v, x1, y1, r, g, b, a);
		push_vert(v, x2, y2, r, g, b, a);
		push_vert(v, x3, y3, r, g, b, a);
	}

	inline void push_quad(float r, float g, float b, float a,
		float x1, float y1, float x2, float y2)
	{
		// Two triangles forming an axis-aligned rectangle.
		auto& v = g_state.tri_verts;
		push_vert(v, x1, y1, r, g, b, a);
		push_vert(v, x2, y1, r, g, b, a);
		push_vert(v, x2, y2, r, g, b, a);
		push_vert(v, x1, y1, r, g, b, a);
		push_vert(v, x1, y2, r, g, b, a);
		push_vert(v, x2, y2, r, g, b, a);
	}

	inline void push_line(float r, float g, float b, float a,
		float x1, float y1, float x2, float y2)
	{
		auto& v = g_state.line_verts;
		push_vert(v, x1, y1, r, g, b, a);
		push_vert(v, x2, y2, r, g, b, a);
	}

	// Draw a small caret/chevron on the player body so the facing
	// direction is readable. A filled triangle is centered on the
	// rectangle and points in the facing direction. Color is always
	// black — the callers set the alpha. All coordinates must be in
	// screen space (already camera-transformed).
	inline void draw_facing_indicator(float r, float g, float b, float a,
		float left, float top, float right, float bottom, int8_t facing)
	{
		if (facing == 0) return;
		const float cx = (left + right) * 0.5f;
		const float cy = (top + bottom) * 0.5f;
		const float hw = 6.0f;   // half-width of the caret (tip extension)
		const float hh = 12.0f;  // half-height of the caret (vertical base)
		if (facing > 0)
		{
			// Right-pointing triangle, centered on body.
			push_tri(r, g, b, a,
				cx + hw, cy,       // tip (right)
				cx - hw, cy - hh,  // top-left
				cx - hw, cy + hh); // bottom-left
		}
		else
		{
			// Left-pointing triangle, centered on body.
			push_tri(r, g, b, a,
				cx - hw, cy,       // tip (left)
				cx + hw, cy - hh,  // top-right
				cx + hw, cy + hh); // bottom-right
		}
	}

	void ensure_vbo_capacity(GLsizei needed_verts)
	{
		if (needed_verts <= g_state.vbo_capacity_verts) return;

		GLsizei new_cap = g_state.vbo_capacity_verts > 0 ? g_state.vbo_capacity_verts : 1024;
		while (new_cap < needed_verts) new_cap *= 2;

		glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
		glBufferData(GL_ARRAY_BUFFER, new_cap * k_vert_stride_floats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
		g_state.vbo_capacity_verts = new_cap;
	}

	void draw_buffer(GLenum mode, const std::vector<float>& verts)
	{
		if (verts.empty()) return;
		const GLsizei vert_count = (GLsizei)(verts.size() / k_vert_stride_floats);

		ensure_vbo_capacity(vert_count);

		glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
		glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
		glDrawArrays(mode, 0, vert_count);
	}
}

void draw::init()
{
	if (g_state.initialized) return;

	GLuint vs = compile_shader(GL_VERTEX_SHADER, k_vert_src);
	GLuint fs = compile_shader(GL_FRAGMENT_SHADER, k_frag_src);
	g_state.program = link_program(vs, fs);
	glDeleteShader(vs);
	glDeleteShader(fs);

	g_state.u_proj = glGetUniformLocation(g_state.program, "u_proj");

	glGenVertexArrays(1, &g_state.vao);
	glGenBuffers(1, &g_state.vbo);

	glBindVertexArray(g_state.vao);
	glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);

	const GLsizei stride = k_vert_stride_floats * sizeof(float);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

	// Alpha blending so ghost rendering (Phase 4d, a < 1.0) composites.
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Reserve a reasonable batch upfront so the first frame doesn't reallocate
	// while pushing thousands of tile verts. Tuned to fit a Pitfall-sized map.
	g_state.tri_verts.reserve(64 * 1024);
	g_state.line_verts.reserve(8 * 1024);

	g_state.initialized = true;
}

void draw::flush_frame()
{
	if (!g_state.initialized) return;

	glUseProgram(g_state.program);
	glUniformMatrix4fv(g_state.u_proj, 1, GL_FALSE, g_state.proj);
	glBindVertexArray(g_state.vao);

	draw_buffer(GL_TRIANGLES, g_state.tri_verts);
	draw_buffer(GL_LINES, g_state.line_verts);

	g_state.tri_verts.clear();
	g_state.line_verts.clear();
}

void draw::shutdown()
{
	if (!g_state.initialized) return;
	if (g_state.vbo) glDeleteBuffers(1, &g_state.vbo);
	if (g_state.vao) glDeleteVertexArrays(1, &g_state.vao);
	if (g_state.program) glDeleteProgram(g_state.program);
	g_state = gl_state{};
}

void draw::set_viewport(int width_px, int height_px)
{
	glViewport(0, 0, width_px, height_px);
	make_ortho(g_state.proj, 0.0f, (float)width_px, (float)height_px, 0.0f, -1.0f, 1.0f);
}

void draw::set_local_player_color(float r, float g, float b)
{
	g_state.local_r = r;
	g_state.local_g = g;
	g_state.local_b = b;
}

// ---------------------------------------------------------------------------
// Primitive draws
// ---------------------------------------------------------------------------

void draw::draw_triangle_a(float r, float g, float b, float a, vector p1, vector p2, vector p3)
{
	push_tri(r, g, b, a, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
}

void draw::draw_triangle(float r, float g, float b, vector p1, vector p2, vector p3)
{
	push_tri(r, g, b, 1.0f, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
}

void draw::draw_rectangle_a(float r, float g, float b, float a, vector p1, vector p2)
{
	push_quad(r, g, b, a, p1.x, p1.y, p2.x, p2.y);
}

void draw::draw_rectangle(float r, float g, float b, vector p1, vector p2)
{
	push_quad(r, g, b, 1.0f, p1.x, p1.y, p2.x, p2.y);
}

void draw::draw_rectangle(float r, float g, float b, const aabb& bounds)
{
	// Fixes a long-standing typo from immediate-mode code where two of the
	// six vertices used max_x for the y-coordinate.
	push_quad(r, g, b, 1.0f, bounds.min_x, bounds.min_y, bounds.max_x, bounds.max_y);
}

void draw::draw_line_a(float r, float g, float b, float a, vector p1, vector p2)
{
	push_line(r, g, b, a, p1.x, p1.y, p2.x, p2.y);
}

void draw::draw_line(float r, float g, float b, vector p1, vector p2)
{
	push_line(r, g, b, 1.0f, p1.x, p1.y, p2.x, p2.y);
}

// ---------------------------------------------------------------------------
// Higher-level draws (unchanged behavior; just call new primitives)
// ---------------------------------------------------------------------------

void draw::draw_tile(emu::tile_id tile, vector pos)
{
	// Palette is read from the live visuals_config so OptionsModal
	// edits take effect mid-frame. Defaults match the original
	// medium-gray body + white stripes, designed to keep the
	// "this surface is grappable / climbable" affordance readable.
	const auto& v = draw::visuals();
	const float body_r = v.walls_r, body_g = v.walls_g, body_b = v.walls_b;
	const float gstripe_r = v.grapple_stripe_r, gstripe_g = v.grapple_stripe_g, gstripe_b = v.grapple_stripe_b;
	const float wstripe_r = v.wallclimb_stripe_r, wstripe_g = v.wallclimb_stripe_g, wstripe_b = v.wallclimb_stripe_b;

	switch (tile)
	{
	case tile_air:
		break;
	case tile_square:
	case tile_checkered:
		draw_rectangle(
			body_r, body_g, body_b,
			pos, pos + vector{ 16.0f, 16.0f });
		break;
	case tile_grapple_ceil:
		draw_rectangle(
			body_r, body_g, body_b,
			pos, pos + vector{ 16.0f, 16.0f });
		draw_rectangle(
			gstripe_r, gstripe_g, gstripe_b,
			pos + vector{ 0.0f, 13.0f }, pos + vector{ 16.0f, 16.0f });
		break;
	case tile_wall_right:
		draw_rectangle(
			body_r, body_g, body_b,
			pos, pos + vector{ 16.0f, 16.0f });
		draw_rectangle(
			wstripe_r, wstripe_g, wstripe_b,
			pos + vector{ 13.0f, 0.0f }, pos + vector{ 16.0f, 16.0f });
		break;
	case tile_wall_left:
		draw_rectangle(
			body_r, body_g, body_b,
			pos, pos + vector{ 16.0f, 16.0f });
		draw_rectangle(
			wstripe_r, wstripe_g, wstripe_b,
			pos, pos + vector{ 3.0f, 16.0f });
		break;
	case tile_slope_floor_left:
	case tile_stairs_left:
	case tile_checkered_slope_floor_left:
		draw_triangle(
			body_r, body_g, body_b,
			pos, pos + vector{ 0.0f, 16.0f }, pos + vector{ 16.0f, 16.0f });
		break;
	case tile_slope_floor_right:
	case tile_stairs_right:
	case tile_checkered_slope_floor_right:
		draw_triangle(
			body_r, body_g, body_b,
			pos + vector{ 16.0f, 0.0f }, pos + vector{ 16.0f, 16.0f }, pos + vector{ 0.0f, 16.0f });
		break;
	case tile_slope_ceil_left:
	case tile_checkered_slope_ceil_left:
		draw_triangle(
			body_r, body_g, body_b,
			pos, pos + vector{ 16.0f, 0.0f }, pos + vector{ 0.0f, 16.0f });
		break;
	case tile_slope_ceil_right:
	case tile_checkered_slope_ceil_right:
		draw_triangle(
			body_r, body_g, body_b,
			pos, pos + vector{ 16.0f, 0.0f }, pos + vector{ 16.0f, 16.0f });
		break;
	case tile_count:
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
	// Local player color is overridden via set_local_player_color() once
	// JS sets the identity. Default matches the original red.
	draw::draw_rectangle(
		g_state.local_r, g_state.local_g, g_state.local_b,
		player->get_collision()->get_vertex(0) - camera.position,
		player->get_collision()->get_vertex(2) - camera.position);

	// Facing indicator — black caret on the player body at 10% opacity.
	// Uses move_direction so the player keeps facing the last input
	// direction even when stationary.
	{
		const vector tl = player->get_collision()->get_vertex(1) - camera.position;
		const vector br = player->get_collision()->get_vertex(3) - camera.position;
draw_facing_indicator(0.0f, 0.0f, 0.0f, 0.2f,
		tl.x, tl.y, br.x, br.y,
		static_cast<int8_t>(player->d.move_direction));
	}

	if (draw::visuals().show_boost_bar)
	{
		vector top_left = { (camera.viewport_size.x - 200.0f) / 2, 15.0f };

		draw::draw_rectangle(0.7f, 0.7f, 0.7f, top_left, top_left + vector{ 200.0f, 25.0f });
		draw::draw_rectangle(0.0f, 0.0f, 1.0f, top_left, top_left + vector{ 200.0f * player->d.boost / 2.0f, 25.0f });
	}
}

void draw::draw_grapple(grapple* grapple, const camera& camera)
{
	if (grapple->m_actor->d.is_collision_active)
	{
		const vector hook_p = grapple->get_center() - camera.position;
		const vector owner_p = grapple->m_owner->m_actor->get_collision()->get_center() - camera.position;

		const auto& v = draw::visuals();

		// Rope as a thin oriented quad rather than GL_LINES so it lives in
		// the triangle batch — without that, lines render after triangles
		// in flush_frame() and the rope would end up in front of the
		// player. With draw_state's two-pass ordering (non-players first,
		// then players), this push ends up under the local-player rect.
		const vector dir = hook_p - owner_p;
		const float len = dir.length();
		if (len > 0.001f)
		{
			const float half_w = 1.0f;
			const vector n{ -dir.y / len * half_w, dir.x / len * half_w };
			draw_triangle(v.grapple_cord_r, v.grapple_cord_g, v.grapple_cord_b, owner_p + n, hook_p + n, hook_p - n);
			draw_triangle(v.grapple_cord_r, v.grapple_cord_g, v.grapple_cord_b, owner_p + n, hook_p - n, owner_p - n);
		}

		// Hook tip is rendered centered on the grapple actor's collision
		// center so resizing the visual doesn't decouple the affordance
		// from the actual attachment point.
		const float hs = v.grapple_head_size;
		const vector center = grapple->m_actor->d.position + grapple->m_actor->d.size * 0.5f;
		const vector half{ hs * 0.5f, hs * 0.5f };
		draw::draw_rectangle(
			v.grapple_head_r, v.grapple_head_g, v.grapple_head_b,
			center - half - camera.position,
			center + half - camera.position);
	}
}

void draw::draw_player_start(player_start* player_start, const camera& camera)
{

}

void draw::draw_super_boost_volume(super_boost_volume* super_boost_volume, const camera& camera)
{
	// User-tunable through visuals_config (boost_pickup_*). Defaults to
	// the original 10%-alpha green tint so an untouched client looks the
	// same as before.
	const auto& v = draw::visuals();
	draw::draw_rectangle_a(
		v.boost_pickup_r, v.boost_pickup_g, v.boost_pickup_b, v.boost_pickup_a,
		super_boost_volume->m_actor->m_bounds.get_vertex(0) - camera.position,
		super_boost_volume->m_actor->m_bounds.get_vertex(2) - camera.position);
}

void draw::draw_boost_section(boost_section* boost_section, const camera& camera)
{
	const auto& v = draw::visuals();
	draw::draw_rectangle_a(
		v.boost_section_r, v.boost_section_g, v.boost_section_b, v.boost_section_a,
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

void draw::draw_state_world(state* state, const camera& camera)
{
	if (state->m_collision_engine.m_level != nullptr)
		draw_tile_layer(&(state->m_collision_engine.m_level->m_tile_layer), camera);
	// Non-player actors (grapple rope, boost volumes, obstacles, …). Push
	// order = paint order in the batched triangle stream, so emitting
	// these first guarantees the local player rectangle sits above the
	// grapple rope when both belong to the same player.
	for (auto& actor : state->actors())
	{
		if (dynamic_cast<emu::player*>(actor->m_controller.get()) == nullptr)
			draw_actor_controller(actor->m_controller.get(), camera);
	}
}

void draw::draw_state_players(state* state, const camera& camera)
{
	for (auto& actor : state->actors())
	{
		if (dynamic_cast<emu::player*>(actor->m_controller.get()) != nullptr)
			draw_actor_controller(actor->m_controller.get(), camera);
	}
}

void draw::draw_state(state* state, const camera& camera)
{
	draw_state_world(state, camera);
	draw_state_players(state, camera);
}

void draw::draw_rg_grid(const camera& camera, float min_world_y, float max_world_y)
{
	// One grid cell == one game tile (16 wu). Major lines every 4 tiles
	// (64 wu) read as a coarser anchor; minor lines fill in the rest.
	// Both axes are world-aligned so the cell boundaries land exactly
	// on tile edges. Major-line phase is anchored to the corridor's
	// inner top so the line sitting against the ceiling AND the line
	// sitting against the floor both read as major (corridor height
	// is 20 tiles = 5×4 by construction in load_rg_challenge).
	constexpr float k_minor = 16.0f;
	constexpr int k_major_every = 4;
	constexpr float k_alpha_minor = 0.06f;
	constexpr float k_alpha_major = 0.14f;
	constexpr float k_r = 0.85f, k_g = 0.85f, k_b = 0.90f;

	const float vw = camera.viewport_size.x;
	const float vh = camera.viewport_size.y;
	const float left = camera.position.x;
	const float top = camera.position.y;

	// Clip the corridor band against the viewport in world space, then
	// project to screen space. If the visible band is empty we draw
	// nothing — including the vertical lines, since they should only
	// span the corridor interior.
	const float band_min = std::max(min_world_y, top);
	const float band_max = std::min(max_world_y, top + vh);
	if (!(band_max > band_min)) return;

	const float screen_band_min = band_min - top;
	const float screen_band_max = band_max - top;

	// Tile-row index of the corridor's top wall edge — anchors the
	// major-line phase so that line is major. Walls are tile-aligned by
	// construction, so this maps to an integer.
	const int top_row = static_cast<int>(std::lround(min_world_y / k_minor));

	const int x_first = static_cast<int>(std::floor(left / k_minor));
	const int x_last = static_cast<int>(std::floor((left + vw) / k_minor));
	for (int i = x_first; i <= x_last; i++)
	{
		const float screen_x = static_cast<float>(i) * k_minor - left;
		const float a = (((i - top_row) % k_major_every) == 0) ? k_alpha_major : k_alpha_minor;
		draw_line_a(k_r, k_g, k_b, a,
			vector{ screen_x, screen_band_min },
			vector{ screen_x, screen_band_max });
	}

	const int y_first = static_cast<int>(std::ceil(min_world_y / k_minor));
	const int y_last = static_cast<int>(std::floor(max_world_y / k_minor));
	for (int j = y_first; j <= y_last; j++)
	{
		const float world_y = static_cast<float>(j) * k_minor;
		if (world_y < band_min || world_y > band_max) continue;
		const float screen_y = world_y - top;
		const float a = (((j - top_row) % k_major_every) == 0) ? k_alpha_major : k_alpha_minor;
		draw_line_a(k_r, k_g, k_b, a,
			vector{ 0.0f, screen_y }, vector{ vw, screen_y });
	}
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

void draw::draw_ghost(const net::ghost_state& ghost, const camera& camera)
{
	// Draw the ghost body at half-alpha. Ghost grapple (when active)
	// renders the rope using the same color so it reads as "their hook"
	// rather than world geometry. No HUD bars (boost meter etc), since
	// only the local player owns the HUD.
	const float a = 0.5f;

	// Crop the top of the standing rectangle when the slide bit is set
	// (anim & 1). 20px is the same offset the local C++ uses to swap from
	// the standing hitbox to the sliding one — keeping it in sync means a
	// remote ghost looks identical to the local player on the same input.
	const bool is_sliding = (ghost.anim & 0x1u) != 0;
	const float slide_top_inset_px = 20.0f;
	const float top_inset = is_sliding ? slide_top_inset_px : 0.0f;
	const vector top_left{ ghost.position.x - camera.position.x, ghost.position.y + top_inset - camera.position.y };
	const vector bot_right = ghost.position + ghost.size - camera.position;

	// Push the rope + end markers BEFORE the ghost rectangle so the body
	// sits on top — same convention as the local player.
	if (ghost.grapple_active)
	{
		const vector origin_s = ghost.grapple_origin - camera.position;
		const vector attach_s = ghost.grapple_attach - camera.position;
		// Draw the rope as a thin oriented quad rather than GL_LINES, since
		// WebGL2 only guarantees 1px line width and the ghost color at 50%
		// alpha is otherwise easy to lose against the map. Two triangles
		// along the rope direction, offset perpendicularly by half-thickness.
		const vector dir = attach_s - origin_s;
		const float len = dir.length();
		if (len > 0.001f)
		{
			const float half_w = 1.5f;
			const vector n{ -dir.y / len * half_w, dir.x / len * half_w };
			draw_triangle_a(ghost.color_r, ghost.color_g, ghost.color_b, a,
				origin_s + n, attach_s + n, attach_s - n);
			draw_triangle_a(ghost.color_r, ghost.color_g, ghost.color_b, a,
				origin_s + n, attach_s - n, origin_s - n);
		}
		// Hook head at the attach point (~10px) so it's visually distinct
		// from rope. Filled at full ghost alpha to read as the connection.
		draw_rectangle_a(ghost.color_r, ghost.color_g, ghost.color_b, a,
			attach_s + vector{ -5.0f, -5.0f },
			attach_s + vector{  5.0f,  5.0f });
		// Origin marker at the player end so the rope clearly attaches
		// there even if it overlaps with the ghost body.
		draw_rectangle_a(ghost.color_r, ghost.color_g, ghost.color_b, a,
			origin_s + vector{ -3.0f, -3.0f },
			origin_s + vector{  3.0f,  3.0f });
	}

	draw_rectangle_a(ghost.color_r, ghost.color_g, ghost.color_b, a, top_left, bot_right);

	// Facing indicator for the ghost — same black caret as the local
	// player. Keeps the 10 % opacity independent of ghost body alpha
	// so it reads consistently across all ghosts.
	draw_facing_indicator(0.0f, 0.0f, 0.0f, 0.2f,
		top_left.x, top_left.y, bot_right.x, bot_right.y,
		ghost.facing);
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
