// JS-callable C ABI for the WASM build (and the desktop build, where
// it's exercised by tests + dev tooling). All entry points are extern "C"
// and prefixed sr_; they take only primitives + raw pointers. See
// AGENTS.md for the full convention.

#include <cstdint>
#include <cstring>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "sr_api.h"
#include "../instance.h"
#include "../playground.h"
#include "../drawing/visuals_config.h"
#include "../emulation/player.h"
#include "../emulation/grapple.h"

namespace
{
	instance* g_inst = nullptr;

	// Single-slot save state used by the F5 / F9 quick-save / quick-load
	// keybinds. Captures just enough to make practice runs useful: the
	// position and velocity restore the trajectory; boost restores the
	// charge so the player can keep using their stockpile after a load.
	struct local_save_state
	{
		bool valid = false;
		emu::vector position{ 0, 0 };
		emu::vector velocity{ 0, 0 };
		float boost = 0.0f;
	};
	local_save_state g_save_state;

	// Snapshot wire layout (bytes). Mirrored on the JS side in
	// packages/protocol. Bumping PROTOCOL_VERSION there must be paired
	// with a layout change here.
	//
	// offset  size  field
	// 0       4     pos_x   (float32 LE)
	// 4       4     pos_y
	// 8       4     vel_x
	// 12      4     vel_y
	// 16      1     facing  (int8: -1 / +1)
	// 17      1     anim    (uint8 — visual state, currently always 0)
	// 18      1     grapple_active (0/1)
	// 19      1     grapple_taut   (0/1)
	// 20      4     grapple_origin_x
	// 24      4     grapple_origin_y
	// 28      4     grapple_attach_x
	// 32      4     grapple_attach_y
	// 36      4     grapple_length
	// 40      4     size_x  (player collision width, for ghost rendering)
	// 44      4     size_y  (player collision height)
	// total:  48 bytes
	constexpr std::size_t k_snapshot_bytes = 48;

	template <typename T>
	void write_le(std::uint8_t* dst, T value)
	{
		std::memcpy(dst, &value, sizeof(T));
	}
}

void net::set_active_instance(::instance* inst) { g_inst = inst; }
::instance* net::active_instance() { return g_inst; }

extern "C"
{
	// Set the local player's display name + RGB color (0..1). Called once
	// after WASM startup, then never again unless the user changes it.
	void sr_set_local_identity(const char* name, float r, float g, float b)
	{
		if (g_inst == nullptr || name == nullptr) return;
		auto& id = g_inst->m_playground.m_local_identity;
		id.name = name;
		id.r = r;
		id.g = g;
		id.b = b;
		id.is_set = true;
	}

	// Load a map from a path on the (virtual) filesystem. On the web
	// target maps are preloaded under /maps/ via Emscripten's
	// --preload-file; on desktop they live in game/assets/maps/.
	void sr_load_map(const char* path)
	{
		if (g_inst == nullptr || path == nullptr) return;
		g_inst->m_playground.load(std::string{ path });
		g_inst->m_playground.m_ghosts.clear();
	}

	// Upsert a remote player snapshot. Identity (name + color) must be
	// set separately via sr_set_ghost_identity — keeping them split lets
	// JS push 30Hz state without re-sending the static fields.
	void sr_push_ghost(const char* id,
		float pos_x, float pos_y,
		float vel_x, float vel_y,
		std::int8_t facing, std::uint8_t anim,
		std::uint8_t grapple_active,
		float gx_origin, float gy_origin,
		float gx_attach, float gy_attach,
		float g_length, std::uint8_t g_taut,
		float size_x, float size_y)
	{
		if (g_inst == nullptr || id == nullptr) return;
		g_inst->m_playground.m_ghosts.push(
			std::string{ id },
			emu::vector{ pos_x, pos_y },
			emu::vector{ vel_x, vel_y },
			facing, anim,
			grapple_active != 0,
			emu::vector{ gx_origin, gy_origin },
			emu::vector{ gx_attach, gy_attach },
			g_length, g_taut != 0,
			emu::vector{ size_x, size_y });
	}

	void sr_set_ghost_identity(const char* id, const char* name, float r, float g, float b)
	{
		if (g_inst == nullptr || id == nullptr || name == nullptr) return;
		g_inst->m_playground.m_ghosts.set_identity(std::string{ id }, std::string{ name }, r, g, b);
	}

	void sr_remove_ghost(const char* id)
	{
		if (g_inst == nullptr || id == nullptr) return;
		g_inst->m_playground.m_ghosts.remove(std::string{ id });
	}

	// Serializes the local player's current state into out_buf. Returns
	// the number of bytes written, or 0 if there's no player yet (e.g.
	// before sr_load_map).
	std::size_t sr_get_local_snapshot(std::uint8_t* out_buf, std::size_t buf_size)
	{
		if (g_inst == nullptr || out_buf == nullptr) return 0;
		if (buf_size < k_snapshot_bytes) return 0;

		emu::player* p = g_inst->m_playground.m_player;
		if (p == nullptr || p->m_actor == nullptr) return 0;

		const auto& a = p->m_actor->d;
		const std::int8_t facing = (a.velocity.x >= 0.0f) ? std::int8_t{ 1 } : std::int8_t{ -1 };
		// anim is a bitfield: bit 0 = is_sliding. Future visual states
		// (climbing, stunned, etc.) can claim more bits without bumping
		// PROTOCOL_VERSION. Renderers must mask, not equality-check.
		const std::uint8_t anim = static_cast<std::uint8_t>(p->d.is_sliding ? 1 : 0);

		const bool g_active = (p->m_grapple != nullptr) && p->m_grapple->m_actor != nullptr
			&& p->m_grapple->m_actor->d.is_collision_active;
		emu::vector g_origin{ 0, 0 };
		emu::vector g_attach{ 0, 0 };
		float g_length = 0.0f;
		const std::uint8_t g_taut = (g_active && p->d.is_hooked) ? 1 : 0;
		if (g_active)
		{
			g_origin = p->m_grapple->m_owner->m_actor->get_collision()->get_center();
			g_attach = p->m_grapple->get_center();
			g_length = (g_attach - g_origin).length();
		}

		write_le(out_buf +  0, a.position.x);
		write_le(out_buf +  4, a.position.y);
		write_le(out_buf +  8, a.velocity.x);
		write_le(out_buf + 12, a.velocity.y);
		out_buf[16] = static_cast<std::uint8_t>(facing);
		out_buf[17] = anim;
		out_buf[18] = g_active ? 1 : 0;
		out_buf[19] = g_taut;
		write_le(out_buf + 20, g_origin.x);
		write_le(out_buf + 24, g_origin.y);
		write_le(out_buf + 28, g_attach.x);
		write_le(out_buf + 32, g_attach.y);
		write_le(out_buf + 36, g_length);
		// Always send the *standing* rectangle (top-left + standing size),
		// not the active hitbox. Sliding state rides in the anim bitfield
		// above; the receiving renderer crops the top of the rectangle when
		// the slide bit is set. This keeps the position/size pair stable
		// across slide transitions so JS-side lerp doesn't drift.
		write_le(out_buf + 40, a.size.x);
		write_le(out_buf + 44, a.size.y);
		return k_snapshot_bytes;
	}

	// Returns the screen-space position of a player. id == "" or NULL
	// means the local player. Returns 1 on success (out_x/out_y written),
	// 0 if the player is unknown or off-screen / pre-init.
	int sr_get_player_screen_pos(const char* id, float* out_x, float* out_y)
	{
		if (g_inst == nullptr || out_x == nullptr || out_y == nullptr) return 0;

		const auto& cam = g_inst->m_playground.m_camera;

		if (id == nullptr || id[0] == '\0')
		{
			emu::player* p = g_inst->m_playground.m_player;
			if (p == nullptr || p->m_actor == nullptr) return 0;
			const emu::vector top_left = p->m_actor->d.position - cam.position;
			*out_x = top_left.x + p->m_actor->d.size.x * 0.5f;
			*out_y = top_left.y;
			return 1;
		}

		const auto snap = g_inst->m_playground.m_ghosts.snapshot();
		auto it = snap.find(std::string{ id });
		if (it == snap.end()) return 0;
		const auto& gh = it->second;
		const emu::vector top_left = gh.position - cam.position;
		*out_x = top_left.x + gh.size.x * 0.5f;
		*out_y = top_left.y;
		return 1;
	}

	// Rebind a logical action to a GLFW key code. action is the emu::input
	// enum (0=left, 1=right, 2=jump, 3=grapple, 4=slide, 5=boost, 6=item,
	// 7=swap_item). Out-of-range action or non-positive key are ignored.
	void sr_set_binding(int action, int glfw_key)
	{
		if (action < 0 || action >= static_cast<int>(emu::input_count)) return;
		if (glfw_key <= 0) return;
		input_map[static_cast<std::size_t>(action)] = glfw_key;
	}

	// Snap the local player to (x, y) in WORLD coordinates (i.e. the same
	// space sr_get_local_snapshot writes). Velocity is preserved — set_position
	// goes through player::set_position which handles hitbox sync. Used by
	// the JS-side /tp chat command.
	void sr_teleport_local(float x, float y)
	{
		if (g_inst == nullptr) return;
		emu::player* p = g_inst->m_playground.m_player;
		if (p == nullptr) return;
		p->set_position(emu::vector{ x, y });
	}

	// Snap the local player back to the map's PlayerStart actor and zero
	// their velocity. Bound to the "reset" UI key on the JS side.
	void sr_reset_local()
	{
		if (g_inst == nullptr) return;
		auto& pg = g_inst->m_playground;
		emu::player* p = pg.m_player;
		if (p == nullptr || p->m_actor == nullptr) return;
		const emu::level_actor& start = pg.m_level.get_actor("PlayerStart");
		p->set_position(start.position);
		p->m_actor->d.velocity = emu::vector{ 0.0f, 0.0f };
	}

	// Visual palette setters. Colors are 0..1 floats. Read every frame
	// by instance::draw() / draw_util.cpp so changes feel live with no
	// extra book-keeping. Defaults match the original hardcoded palette,
	// so a JS client that never calls these is visually unchanged.
	void sr_set_visual_bg(float r, float g, float b)
	{
		auto& v = draw::visuals();
		v.bg_r = r; v.bg_g = g; v.bg_b = b;
	}
	void sr_set_visual_walls(float r, float g, float b)
	{
		auto& v = draw::visuals();
		v.walls_r = r; v.walls_g = g; v.walls_b = b;
	}
	void sr_set_visual_grapple_stripe(float r, float g, float b)
	{
		auto& v = draw::visuals();
		v.grapple_stripe_r = r; v.grapple_stripe_g = g; v.grapple_stripe_b = b;
	}
	void sr_set_visual_wallclimb_stripe(float r, float g, float b)
	{
		auto& v = draw::visuals();
		v.wallclimb_stripe_r = r; v.wallclimb_stripe_g = g; v.wallclimb_stripe_b = b;
	}
	void sr_set_visual_grapple_cord(float r, float g, float b)
	{
		auto& v = draw::visuals();
		v.grapple_cord_r = r; v.grapple_cord_g = g; v.grapple_cord_b = b;
	}
	void sr_set_visual_grapple_head(float r, float g, float b)
	{
		auto& v = draw::visuals();
		v.grapple_head_r = r; v.grapple_head_g = g; v.grapple_head_b = b;
	}
	void sr_set_visual_grapple_head_size(float size)
	{
		// Clamp to a sane range — silly to allow 0 (invisible) or values
		// large enough to chew the whole map.
		if (size < 1.0f) size = 1.0f;
		if (size > 64.0f) size = 64.0f;
		draw::visuals().grapple_head_size = size;
	}
	void sr_set_visual_boost_section(float r, float g, float b, float a)
	{
		auto& v = draw::visuals();
		v.boost_section_r = r; v.boost_section_g = g; v.boost_section_b = b; v.boost_section_a = a;
	}
	void sr_set_visual_boost_pickup(float r, float g, float b, float a)
	{
		auto& v = draw::visuals();
		v.boost_pickup_r = r; v.boost_pickup_g = g; v.boost_pickup_b = b; v.boost_pickup_a = a;
	}

	// Quick-save the local player's pose so the JS side can later
	// quick-load it. Captures position + velocity + boost charge — enough
	// for speedrun practice without smashing other simulation state.
	void sr_save_state()
	{
		if (g_inst == nullptr) return;
		emu::player* p = g_inst->m_playground.m_player;
		if (p == nullptr || p->m_actor == nullptr) return;
		g_save_state.valid = true;
		g_save_state.position = p->m_actor->d.position;
		g_save_state.velocity = p->m_actor->d.velocity;
		g_save_state.boost = p->d.boost;
	}

	// Restore whatever sr_save_state captured. Returns 0 if there's no
	// save yet (so the JS side can decide whether to show a hint).
	int sr_load_state()
	{
		if (g_inst == nullptr) return 0;
		if (!g_save_state.valid) return 0;
		emu::player* p = g_inst->m_playground.m_player;
		if (p == nullptr || p->m_actor == nullptr) return 0;
		p->set_position(g_save_state.position);
		p->m_actor->d.velocity = g_save_state.velocity;
		p->d.boost = g_save_state.boost;
		return 1;
	}

	// Configure the WASM main loop's frame pacing.
	//   fps <= 0  → use requestAnimationFrame (monitor refresh, default)
	//   fps > 0   → use setTimeout(1000/fps) — lets the user uncap above
	//              monitor refresh OR throttle below it for low-end machines.
	void sr_set_target_fps(int fps)
	{
#ifdef __EMSCRIPTEN__
		if (fps <= 0)
		{
			emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
			return;
		}
		const int ms = 1000 / fps;
		emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, ms < 1 ? 1 : ms);
#else
		(void)fps;
#endif
	}
}
