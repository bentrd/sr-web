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

	// Submit all queued geometry as ONE glDrawArrays per primitive mode
	// (triangles + lines). Call once per frame after every draw_*. The
	// per-primitive submit_*() calls just push into CPU buffers — nothing
	// hits the GL until flush_frame.
	void flush_frame();

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
	// Split halves of draw_state. Use these when something needs to render
	// between world-space geometry and the local player (e.g. trails) — call
	// flush_frame() between them so the inserted pass appears in the right
	// z-order. draw_state() itself just chains both for convenience.
	void draw_state_world(emu::state* state, const camera& camera);
	void draw_state_players(emu::state* state, const camera& camera);

	void draw_right_pot_map(const util::level_prep& prep, const camera& camera);
	void draw_left_pot_map(const util::level_prep& prep, const camera& camera);

	// Subtle world-aligned grid at 16 wu spacing, used as an optional
	// motion reference in the RG challenge corridor. Clipped to the
	// world-space y range [min_world_y, max_world_y] so the grid only
	// appears between the corridor's ceiling and floor tiles. Caller
	// decides when to invoke (typically before draw_state_world so it
	// sits behind tiles).
	void draw_rg_grid(const camera& camera, float min_world_y, float max_world_y);

	// Highlight band marking the time-challenge goal line. Drawn behind
	// the wall so the wall tile reads as the actual stop, with a glowing
	// vertical strip just before it cueing "you're at the end". Both
	// world-space x bounds describe a vertical region; the band is
	// clipped to the corridor interior given by [min_world_y, max_world_y].
	void draw_time_goal(const camera& camera,
		float world_x_start, float world_x_end,
		float min_world_y, float max_world_y);

	// Render a single remote-player snapshot at half-alpha. Phase 4d.
	void draw_ghost(const net::ghost_state& ghost, const camera& camera);
}

#endif
