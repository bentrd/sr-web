#ifndef DRAW_UTIL_H
#define DRAW_UTIL_H

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif
#include <GLFW/glfw3.h>

#include "camera.h"
#include "../emulation/vector.h"
#include "../emulation/aabb.h"
#include "../emulation/player.h"
#include "../emulation/grapple.h"
#include "../emulation/player_start.h"
#include "../emulation/super_boost_volume.h"
#include "../emulation/boost_section.h"
#include "../emulation/obstacle.h"
#include "../emulation/state.h"
#include "../emulation/tile_layer_base.h"
#include "../utility/level_preprocessing.h"
#include "../network/ghost_state.h"

namespace draw
{
	// Lifecycle. init() is idempotent — safe to call after every context
	// creation. set_viewport() must be called after init() and whenever the
	// window resizes; it updates the internal ortho projection (origin
	// top-left, +y down) and calls glViewport.
	void init();
	void shutdown();
	void set_viewport(int width_px, int height_px);

	// Override the default red used for the local-player rectangle.
	// Called by the renderer once per frame when a local identity is set
	// from JS (sr_set_local_identity). Persists until called again.
	void set_local_player_color(float r, float g, float b);

	void draw_triangle(float r, float g, float b, emu::vector p1, emu::vector p2, emu::vector p3);
	void draw_rectangle(float r, float g, float b, const emu::aabb& bounds);
	void draw_rectangle(float r, float g, float b, emu::vector p1, emu::vector p2);
	void draw_line(float r, float g, float b, emu::vector p1, emu::vector p2);

	// Alpha-aware variants used by ghost rendering (Phase 4d).
	void draw_triangle_a(float r, float g, float b, float a, emu::vector p1, emu::vector p2, emu::vector p3);
	void draw_rectangle_a(float r, float g, float b, float a, emu::vector p1, emu::vector p2);
	void draw_line_a(float r, float g, float b, float a, emu::vector p1, emu::vector p2);

	void draw_tile(emu::tile_id tile, emu::vector pos);
	void draw_tile_layer(emu::tile_layer_base* tile_Layer, const camera& camera);
	void draw_player(emu::player* player, const camera& camera);
	void draw_grapple(emu::grapple* grapple, const camera& camera);
	void draw_player_start(emu::player_start* player_start, const camera& camera);
	void draw_super_boost_volume(emu::super_boost_volume* super_boost_volume, const camera& camera);
	void draw_boost_section(emu::boost_section* boost_section, const camera& camera);
	void draw_obstacle(emu::obstacle* obstacle, const camera& camera);
	void draw_actor_controller(emu::i_actor_controller* controller, const camera& camera);
	void draw_state(emu::state* state, const camera& camera);

	void draw_right_pot_map(const util::level_prep& prep, const camera& camera);
	void draw_left_pot_map(const util::level_prep& prep, const camera& camera);

	// Render a single remote-player snapshot at half-alpha. Phase 4d.
	void draw_ghost(const net::ghost_state& ghost, const camera& camera);
}

#endif
