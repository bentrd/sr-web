#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif
#include <GLFW/glfw3.h>

#include "trail.h"

// ---------------------------------------------------------------------------
// Trail rendering — textured triangle strips per layer.
//
// Each layer keeps a deque-style sample buffer in world coords + age.
// record_sample() ages every sample, evicts expired ones, and pushes a new
// one (subject to a 60 Hz throttle and an optional speed gate). draw_all()
// walks each layer's samples splitting on `strip_start` and emits one
// triangle strip per contiguous run, two verts (top/bottom) per sample
// offset along the segment normal by half the layer's width.
//
// We use a separate shader from draw_util.cpp because trails are textured
// and the main batch is flat-shaded. Switching programs between the two
// passes is fine — it's two glUseProgram calls per frame, dwarfed by the
// CPU cost of rebuilding the strip mesh.
// ---------------------------------------------------------------------------

namespace
{
#ifdef __EMSCRIPTEN__
	const char* k_vert_src =
		"#version 300 es\n"
		"precision highp float;\n"
		"layout(location = 0) in vec2 a_pos;\n"
		"layout(location = 1) in vec2 a_uv;\n"
		"layout(location = 2) in vec4 a_color;\n"
		"uniform mat4 u_proj;\n"
		"out vec2 v_uv;\n"
		"out vec4 v_color;\n"
		"void main() {\n"
		"  v_uv = a_uv;\n"
		"  v_color = a_color;\n"
		"  gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
		"}\n";

	const char* k_frag_src =
		"#version 300 es\n"
		"precision highp float;\n"
		"in vec2 v_uv;\n"
		"in vec4 v_color;\n"
		"uniform sampler2D u_tex;\n"
		"out vec4 o_color;\n"
		"void main() { o_color = texture(u_tex, v_uv) * v_color; }\n";
#else
	const char* k_vert_src =
		"#version 330 core\n"
		"layout(location = 0) in vec2 a_pos;\n"
		"layout(location = 1) in vec2 a_uv;\n"
		"layout(location = 2) in vec4 a_color;\n"
		"uniform mat4 u_proj;\n"
		"out vec2 v_uv;\n"
		"out vec4 v_color;\n"
		"void main() {\n"
		"  v_uv = a_uv;\n"
		"  v_color = a_color;\n"
		"  gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
		"}\n";

	const char* k_frag_src =
		"#version 330 core\n"
		"in vec2 v_uv;\n"
		"in vec4 v_color;\n"
		"uniform sampler2D u_tex;\n"
		"out vec4 o_color;\n"
		"void main() { o_color = texture(u_tex, v_uv) * v_color; }\n";
#endif

	// 8 floats per vertex: x, y, u, v, r, g, b, a.
	constexpr GLsizei k_stride_floats = 8;

	// Sample on every sim tick (300 Hz). Throttling to 60 Hz looked
	// stuttery near the player on >60 Hz monitors because the visible
	// trail head was up to one render frame behind the live rectangle.
	// 300 samples/sec × 2s lifetime × 4 layers ≈ 38 KB of vertex data per
	// frame — cheap enough that we don't need a throttle.
	constexpr float k_sample_period = 0.0f;

	// Strip break threshold. If samples skipped a window longer than this
	// (e.g. player sat below the superspeed gate for >50ms), the next push
	// starts a new strip rather than connecting through the dead zone.
	constexpr float k_strip_break_seconds = 0.05f;

	// Speed gate for ONLY AT SUPERSPEED layers. SR's "trail speed" sits
	// at the green band of our speedometer (>= 750 px/s), and the gate is
	// horizontal-only so wall-running doesn't sneak the trail on while
	// the player is barely moving forward.
	constexpr float k_superspeed_threshold = 750.0f;

	struct sample
	{
		emu::vector pos{ 0.0f, 0.0f };
		float age = 0.0f;
		bool strip_start = false;
	};

	struct layer_data
	{
		std::string image_name;
		int enabled_mode = 0;
		float lifetime = 1.0f;
		float color[3] = { 1.0f, 1.0f, 1.0f };
		float opacity = 1.0f;
		float size = 32.0f;
		bool fade_out = false;
		float fade_out_speed = 1.0f;
		bool taper = false;
		bool flip_h = false;
		bool flip_v = false;
		bool force_right_side_up = false;
		emu::vector offset{ 0.0f, 0.0f };
		bool invert_offset = false;

		std::vector<sample> samples;
		float time_since_last_push = 1.0f;  // > k_sample_period so first tick records
	};

	struct image_tex
	{
		GLuint tex = 0;
		int w = 0;
		int h = 0;
	};

	struct trail_state
	{
		GLuint program = 0;
		GLuint vao = 0;
		GLuint vbo = 0;
		GLint u_proj = -1;
		GLint u_tex = -1;
		GLsizei vbo_capacity_verts = 0;
		std::vector<float> verts;  // scratch — cleared per strip
		std::unordered_map<std::string, image_tex> images;
		std::vector<layer_data> layers;
		bool initialized = false;
	};

	trail_state g;

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
			std::fprintf(stderr, "trail: shader compile failed: %s\n", log);
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
			std::fprintf(stderr, "trail: program link failed: %s\n", log);
			std::exit(1);
		}
		return prog;
	}

	void make_ortho(float* out, float left, float right, float bottom, float top)
	{
		// Mirrors draw_util's make_ortho with near=-1, far=1 baked in.
		const float rl = right - left;
		const float tb = top - bottom;
		out[0]  = 2.0f / rl;  out[1]  = 0.0f;        out[2]  = 0.0f;   out[3]  = 0.0f;
		out[4]  = 0.0f;       out[5]  = 2.0f / tb;   out[6]  = 0.0f;   out[7]  = 0.0f;
		out[8]  = 0.0f;       out[9]  = 0.0f;        out[10] = -1.0f;  out[11] = 0.0f;
		out[12] = -(right + left) / rl;
		out[13] = -(top + bottom) / tb;
		out[14] = 0.0f;
		out[15] = 1.0f;
	}

	void ensure_vbo_capacity(GLsizei needed)
	{
		if (needed <= g.vbo_capacity_verts) return;
		GLsizei cap = g.vbo_capacity_verts > 0 ? g.vbo_capacity_verts : 256;
		while (cap < needed) cap *= 2;
		glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
		glBufferData(GL_ARRAY_BUFFER, cap * k_stride_floats * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
		g.vbo_capacity_verts = cap;
	}

	inline void push_vert(float x, float y, float u, float v, float r, float gn, float bl, float a)
	{
		g.verts.push_back(x); g.verts.push_back(y);
		g.verts.push_back(u); g.verts.push_back(v);
		g.verts.push_back(r); g.verts.push_back(gn); g.verts.push_back(bl); g.verts.push_back(a);
	}
}

void trail::init()
{
	if (g.initialized) return;

	GLuint vs = compile_shader(GL_VERTEX_SHADER, k_vert_src);
	GLuint fs = compile_shader(GL_FRAGMENT_SHADER, k_frag_src);
	g.program = link_program(vs, fs);
	glDeleteShader(vs);
	glDeleteShader(fs);

	g.u_proj = glGetUniformLocation(g.program, "u_proj");
	g.u_tex  = glGetUniformLocation(g.program, "u_tex");

	glGenVertexArrays(1, &g.vao);
	glGenBuffers(1, &g.vbo);

	glBindVertexArray(g.vao);
	glBindBuffer(GL_ARRAY_BUFFER, g.vbo);

	const GLsizei stride = k_stride_floats * sizeof(float);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));

	g.verts.reserve(2 * 1024);

	g.initialized = true;
}

void trail::shutdown()
{
	if (!g.initialized) return;
	for (auto& [name, im] : g.images)
		if (im.tex) glDeleteTextures(1, &im.tex);
	g.images.clear();
	g.layers.clear();
	if (g.vbo) glDeleteBuffers(1, &g.vbo);
	if (g.vao) glDeleteVertexArrays(1, &g.vao);
	if (g.program) glDeleteProgram(g.program);
	g = trail_state{};
}

void trail::clear()
{
	for (auto& [name, im] : g.images)
		if (im.tex) glDeleteTextures(1, &im.tex);
	g.images.clear();
	g.layers.clear();
}

void trail::register_image(const char* name, int w, int h, const std::uint8_t* rgba)
{
	if (!g.initialized || name == nullptr || rgba == nullptr) return;
	if (w <= 0 || h <= 0) return;

	std::string key{ name };
	auto it = g.images.find(key);
	if (it != g.images.end() && it->second.tex)
		glDeleteTextures(1, &it->second.tex);

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	g.images[key] = image_tex{ tex, w, h };
}

void trail::add_layer(
	const char* image_name,
	int enabled_mode,
	float lifetime_seconds,
	float color_r, float color_g, float color_b,
	float opacity,
	float size_px,
	bool fade_out, float fade_out_speed,
	bool taper,
	bool flip_h, bool flip_v, bool force_right_side_up,
	float offset_x, float offset_y, bool invert_offset)
{
	layer_data L;
	L.image_name = image_name ? image_name : "";
	L.enabled_mode = enabled_mode;
	L.lifetime = std::max(0.001f, lifetime_seconds);
	L.color[0] = color_r; L.color[1] = color_g; L.color[2] = color_b;
	L.opacity = std::clamp(opacity, 0.0f, 1.0f);
	L.size = std::max(1.0f, size_px);
	L.fade_out = fade_out;
	L.fade_out_speed = std::max(0.0f, fade_out_speed);
	L.taper = taper;
	L.flip_h = flip_h;
	L.flip_v = flip_v;
	L.force_right_side_up = force_right_side_up;
	L.offset = emu::vector{ offset_x, offset_y };
	L.invert_offset = invert_offset;
	g.layers.push_back(std::move(L));
}

void trail::record_sample(emu::vector pos, emu::vector vel, float dt_seconds)
{
	if (!g.initialized || g.layers.empty()) return;

	for (auto& L : g.layers)
	{
		// Age existing samples; evict any past lifetime from the front
		// (samples are stored oldest-first, so erase(begin) is correct
		// despite being O(n) — N stays under ~120 in practice).
		for (auto& s : L.samples) s.age += dt_seconds;
		while (!L.samples.empty() && L.samples.front().age >= L.lifetime)
			L.samples.erase(L.samples.begin());

		L.time_since_last_push += dt_seconds;
		if (L.time_since_last_push < k_sample_period) continue;

		// SR's superspeed condition is horizontal-only: a player sliding
		// down a wall has plenty of vy but isn't running fast enough to
		// merit a trail. std::abs handles both directions of travel.
		const bool speed_ok = (L.enabled_mode == 0) ||
			(std::abs(vel.x) >= k_superspeed_threshold);
		if (!speed_ok) continue;

		// invert_offset flips the X offset based on facing — the trail
		// asymmetry stays attached to the player's "rear" rather than a
		// fixed world side. Negative vel.x means the player is moving
		// left, so invert.
		emu::vector off = L.offset;
		if (L.invert_offset && vel.x < 0.0f) off.x = -off.x;

		const bool strip_start = L.samples.empty() ||
			L.time_since_last_push > k_strip_break_seconds;

		L.samples.push_back(sample{
			emu::vector{ pos.x + off.x, pos.y + off.y },
			0.0f,
			strip_start });
		L.time_since_last_push = 0.0f;
	}
}

void trail::draw_all(const draw::camera& cam)
{
	if (!g.initialized || g.layers.empty()) return;
	if (cam.viewport_size.x <= 0.0f || cam.viewport_size.y <= 0.0f) return;

	float proj[16];
	make_ortho(proj, 0.0f, cam.viewport_size.x, cam.viewport_size.y, 0.0f);

	glUseProgram(g.program);
	glUniformMatrix4fv(g.u_proj, 1, GL_FALSE, proj);
	if (g.u_tex >= 0) glUniform1i(g.u_tex, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindVertexArray(g.vao);

	for (auto& L : g.layers)
	{
		if (L.samples.size() < 2) continue;
		auto img_it = g.images.find(L.image_name);
		if (img_it == g.images.end()) continue;
		glBindTexture(GL_TEXTURE_2D, img_it->second.tex);

		// Walk samples, splitting at strip_start markers. Each contiguous
		// run becomes one TRIANGLE_STRIP draw call. A single sample isn't
		// drawable on its own — the ribbon needs two endpoints to have a
		// direction, so runs of length 1 are skipped silently.
		std::size_t run_begin = 0;
		while (run_begin < L.samples.size())
		{
			std::size_t run_end = run_begin + 1;
			while (run_end < L.samples.size() && !L.samples[run_end].strip_start)
				++run_end;
			const std::size_t n = run_end - run_begin;

			if (n >= 2)
			{
				g.verts.clear();
				for (std::size_t k = 0; k < n; ++k)
				{
					const std::size_t i = run_begin + k;
					emu::vector dir;
					if (k == 0)
						dir = emu::vector{ L.samples[i + 1].pos.x - L.samples[i].pos.x,
											L.samples[i + 1].pos.y - L.samples[i].pos.y };
					else if (k + 1 == n)
						dir = emu::vector{ L.samples[i].pos.x - L.samples[i - 1].pos.x,
											L.samples[i].pos.y - L.samples[i - 1].pos.y };
					else
						dir = emu::vector{ L.samples[i + 1].pos.x - L.samples[i - 1].pos.x,
											L.samples[i + 1].pos.y - L.samples[i - 1].pos.y };

					const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
					if (len < 0.001f) continue;
					// Perpendicular (rotated 90° CCW), normalized.
					const float nx = -dir.y / len;
					const float ny =  dir.x / len;

					const float t = L.samples[i].age / L.lifetime;
					const float taper_factor = L.taper ? std::max(0.0f, 1.0f - t) : 1.0f;
					const float half_w = L.size * 0.5f * taper_factor;

					float alpha = L.opacity;
					if (L.fade_out)
						alpha *= std::max(0.0f, 1.0f - t * L.fade_out_speed);

					float u_norm = (n > 1) ? (static_cast<float>(k) / static_cast<float>(n - 1)) : 0.0f;
					if (L.flip_h) u_norm = 1.0f - u_norm;
					const float v_top = L.flip_v ? 1.0f : 0.0f;
					const float v_bot = L.flip_v ? 0.0f : 1.0f;

					const float px = L.samples[i].pos.x;
					const float py = L.samples[i].pos.y;
					const float top_x = px + nx * half_w - cam.position.x;
					const float top_y = py + ny * half_w - cam.position.y;
					const float bot_x = px - nx * half_w - cam.position.x;
					const float bot_y = py - ny * half_w - cam.position.y;

					push_vert(top_x, top_y, u_norm, v_top, L.color[0], L.color[1], L.color[2], alpha);
					push_vert(bot_x, bot_y, u_norm, v_bot, L.color[0], L.color[1], L.color[2], alpha);
				}

				const GLsizei vert_count = static_cast<GLsizei>(g.verts.size() / k_stride_floats);
				if (vert_count >= 2)
				{
					ensure_vbo_capacity(vert_count);
					glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
					glBufferSubData(GL_ARRAY_BUFFER, 0,
						g.verts.size() * sizeof(float), g.verts.data());
					glDrawArrays(GL_TRIANGLE_STRIP, 0, vert_count);
				}
			}
			run_begin = run_end;
		}
	}
}
