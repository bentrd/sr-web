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

	// Sample at 60 Hz (matches SR's game-frame cadence — see
	// SR.exe TrailDraw caller). Between samples we don't drop motion;
	// the SetLastPoint-style code in record_sample MOVES the existing
	// head to track the player exactly. So the head stays pinned to
	// the live player position even on >60 Hz monitors, while the
	// body keeps SR's sparse-sample geometry that the textures and
	// the U-coord math were designed for. Sampling every sim tick
	// (3.33ms) was producing 5x more vertices than SR ever sees,
	// which compressed the U coord and exposed every numerical
	// quirk in the normal computation as a visible artifact.
	constexpr float k_sample_period = 1.0f / 60.0f;

	// Strip break threshold. If samples skipped a window longer than this,
	// the next push starts a new strip rather than connecting through the
	// dead zone. Set generous (300ms) so brief gate dropouts at jump
	// apices — where hypot(vx,vy) can dip under 800 for a few ticks even
	// during a continuous run — don't fragment the trail into a chain of
	// short strips, which produces visible "knot" artifacts where the
	// bright texture head of one strip meets the tail of the next.
	constexpr float k_strip_break_seconds = 0.3f;

	// Two binary speed gates. ALWAYS layers turn on at hypot(vx,vy) >= 800
	// (so a high-arc jump or a wallride still trails); ONLY_AT_SUPERSPEED
	// layers gate strictly on |vx| >= 1200 because that's the
	// "horizontal-runway-supersonic" effect, not "going fast in any
	// direction". Boost bypasses both: pressing the button kicks the trail
	// in immediately, regardless of the player's current speed.
	constexpr float k_trail_on_threshold       = 800.0f;
	constexpr float k_superspeed_on_threshold  = 1200.0f;

	struct sample
	{
		emu::vector pos{ 0.0f, 0.0f };
		emu::vector vel{ 0.0f, 0.0f };
		// Distance from the previous sample in the same strip. SR uses
		// this to map U-coords by accumulated path length instead of by
		// vertex index — without it, dense bunching at slow points
		// compresses the texture (and stretches it across sparse fast
		// regions), which is exactly what produced the bright "wing"
		// artifacts at peaks.
		float seg_length = 0.0f;
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

void trail::record_sample(emu::vector pos, emu::vector vel, float dt_seconds, bool boosting)
{
	if (!g.initialized || g.layers.empty()) return;

	const float speed_xy = std::sqrt(vel.x * vel.x + vel.y * vel.y);
	const float speed_x  = std::abs(vel.x);

	for (auto& L : g.layers)
	{
		// Age existing samples; evict any past lifetime from the front
		// (samples are stored oldest-first, so erase(begin) is correct
		// despite being O(n) — N stays under ~120 in practice).
		for (auto& s : L.samples) s.age += dt_seconds;
		while (!L.samples.empty() && L.samples.front().age >= L.lifetime)
			L.samples.erase(L.samples.begin());

		L.time_since_last_push += dt_seconds;

		// Per-layer record-time gate. ALWAYS layers (enabled_mode==0)
		// gate on hypot(vx,vy) so a pure-vertical climb still trails,
		// AND boost forces them on regardless of speed (the user wants
		// the base trail visible the moment boost is tapped).
		// SUPERSPEED layers gate strictly on |vx| >= 1200 — boost does
		// NOT trigger them; they're meant to read as "the player has
		// reached actual supersonic horizontal speed". Once emitted,
		// samples age out independently of current speed.
		bool gate_open;
		if (L.enabled_mode == 0)
			gate_open = boosting || (speed_xy >= k_trail_on_threshold);
		else
			gate_open = (speed_x >= k_superspeed_on_threshold);
		if (!gate_open) continue;

		// invert_offset flips the X offset based on facing — the trail
		// asymmetry stays attached to the player's "rear" rather than a
		// fixed world side. Negative vel.x means the player is moving
		// left, so invert.
		emu::vector off = L.offset;
		if (L.invert_offset && vel.x < 0.0f) off.x = -off.x;
		const emu::vector new_pos{ pos.x + off.x, pos.y + off.y };

		// SR uses SetLastPoint between AddPoint calls to MOVE the head
		// to the live player position. We mirror that: while the
		// sample period hasn't elapsed, just slide the last sample.
		// This keeps the visible head pinned to the player at render
		// rate even though we only emit a new vertex every ~16.67ms.
		if (L.time_since_last_push < k_sample_period)
		{
			if (!L.samples.empty())
			{
				auto& last = L.samples.back();
				last.pos = new_pos;
				if (!last.strip_start && L.samples.size() >= 2)
				{
					const auto& prev = L.samples[L.samples.size() - 2];
					const float dx = new_pos.x - prev.pos.x;
					const float dy = new_pos.y - prev.pos.y;
					last.seg_length = std::sqrt(dx * dx + dy * dy);
				}
			}
			continue;
		}

		const bool strip_start = L.samples.empty() ||
			L.time_since_last_push > k_strip_break_seconds;

		float seg_length = 0.0f;
		if (!strip_start && !L.samples.empty())
		{
			const auto& prev = L.samples.back();
			const float dx = new_pos.x - prev.pos.x;
			const float dy = new_pos.y - prev.pos.y;
			seg_length = std::sqrt(dx * dx + dy * dy);
		}

		L.samples.push_back(sample{
			new_pos,
			vel,
			seg_length,
			0.0f,
			strip_start });
		L.time_since_last_push = 0.0f;
	}
}

void trail::draw_all(const draw::camera& cam, float /*current_abs_vx*/)
{
	if (!g.initialized || g.layers.empty()) return;
	if (cam.viewport_size.x <= 0.0f || cam.viewport_size.y <= 0.0f) return;

	// No draw-time speed gate. Visibility was decided at record_sample —
	// every sample in the buffer earned its place by passing its layer's
	// gate at the moment it was emitted, and from there ages out on its
	// own clock independent of what the player is doing right now.

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

				// Total path length of this strip — denominator for the
				// U coordinate. Skips the first sample because its
				// seg_length is from the previous strip (or zero for a
				// fresh strip). SR's algorithm — texture distributes by
				// distance traveled, not by sample count.
				float total_length = 0.0f;
				for (std::size_t k = 1; k < n; ++k)
					total_length += L.samples[run_begin + k].seg_length;
				if (total_length < 0.001f) { run_begin = run_end; continue; }

				// InvertNormal: SR flips the V coord per-strip when the
				// trail's first segment's perpendicular points down in
				// world space. Without this the texture appears
				// mirrored when the player runs leftward (or any
				// direction that makes the perpendicular's Y negative).
				// Computed from the second sample's neighbor-difference
				// direction, mirroring SR's nYaTtVj normal calc.
				bool invert_v_strip = false;
				if (!L.force_right_side_up && n >= 2)
				{
					const float dx = L.samples[run_begin + 1].pos.x - L.samples[run_begin].pos.x;
					const float dy = L.samples[run_begin + 1].pos.y - L.samples[run_begin].pos.y;
					const float dl = std::sqrt(dx * dx + dy * dy);
					if (dl > 0.001f)
					{
						const float first_normal_y = dx / dl;  // perp.y = dir.x / |dir|
						invert_v_strip = (first_normal_y > 0.0f);
					}
				}

				float accum_length = 0.0f;
				for (std::size_t k = 0; k < n; ++k)
				{
					const std::size_t i = run_begin + k;

					// Centered-difference normal — SR's nYaTtVj style.
					// At strip boundaries we treat self as the missing
					// neighbor (so a 1-sided difference falls out of
					// the same formula).
					const std::size_t i_prev = (k == 0) ? i : i - 1;
					const std::size_t i_next = (k + 1 == n) ? i : i + 1;
					float dx = L.samples[i_next].pos.x - L.samples[i_prev].pos.x;
					float dy = L.samples[i_next].pos.y - L.samples[i_prev].pos.y;
					float dl = std::sqrt(dx * dx + dy * dy);
					if (dl < 0.001f)
					{
						// Fall back to stored velocity if neighbors
						// collapsed (apex of vertical jump etc.).
						const auto& v = L.samples[i].vel;
						const float vl = std::sqrt(v.x * v.x + v.y * v.y);
						if (vl < 0.001f) { accum_length += L.samples[i].seg_length; continue; }
						dx = v.x; dy = v.y; dl = vl;
					}
					// Perpendicular (90° CCW), normalized — matches SR:
					// normal.X = -dir.Y, normal.Y = dir.X.
					const float nx = -dy / dl;
					const float ny =  dx / dl;

					if (k > 0) accum_length += L.samples[i].seg_length;

					const float t = L.samples[i].age / L.lifetime;
					const float taper_factor = L.taper ? std::max(0.0f, 1.0f - t) : 1.0f;
					const float half_w = L.size * 0.5f * taper_factor;

					float alpha = L.opacity;
					if (L.fade_out)
						alpha *= std::max(0.0f, 1.0f - t * L.fade_out_speed);

					float u_norm = accum_length / total_length;
					if (L.flip_h) u_norm = 1.0f - u_norm;

					// V is flipped both by the layer's flip_v setting
					// AND by the per-strip InvertNormal. The XOR keeps
					// the two flips composable.
					const bool v_flipped = L.flip_v ^ invert_v_strip;
					const float v_top = v_flipped ? 1.0f : 0.0f;
					const float v_bot = v_flipped ? 0.0f : 1.0f;

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
