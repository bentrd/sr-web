#ifndef TRAIL_H
#define TRAIL_H

#include <cstdint>

#include "camera.h"
#include "../emulation/vector.h"

// Textured ribbon trails, organized into named tracks. Track id "" (or
// nullptr) is the local player; each remote player gets their own track
// keyed by ghost id. Tracks own their textures and layers, so two players
// using different .srt files with overlapping image names ("13.png")
// get separate textures with no collision.
//
// Image data is owned per-track — each register_image() upload becomes a
// GL texture; clear_track() releases that track's textures + layers;
// clear() drops every track. JS owns the source PNGs and the .trail
// layer config; this subsystem is pure render machinery.
namespace trail
{
	void init();
	void shutdown();

	// Drop every track (every layer + every uploaded image).
	void clear();

	// Drop one track. track_id == nullptr or "" means the local player.
	// Called when a ghost leaves the room (free their textures) or when
	// a peer rebroadcasts a fresh .srt (clear before re-loading).
	void clear_track(const char* track_id);

	// Per-track opacity multiplier, applied to every vertex's alpha at
	// draw time. Default 1.0 — set to 0.5 for ghost tracks so their
	// trails match the half-opacity ghost rectangle. Clamped to [0, 1].
	void set_track_opacity(const char* track_id, float opacity);

	// Per-track visibility flag. Default true — set to false to hide
	// every layer in this track without dropping its samples (so a
	// "show other players' trails" toggle can flick back on cleanly).
	void set_track_visible(const char* track_id, bool visible);

	// Upload a 32-bit RGBA texture into the named track and key it by
	// `name`. Layers in the same track reference images by the same
	// name. Re-registering replaces the previous texture in that track.
	void register_image(const char* track_id,
		const char* name, int w, int h, const std::uint8_t* rgba);

	// Append a layer to the named track. enabled_mode:
	//   0 = ALWAYS (sample every record_sample tick once gate opens)
	//   1 = ONLY AT SUPERSPEED (only sample when |vx| >= 1200 px/s)
	// Properties match the .trail file layout 1:1 — see parseTrail.ts.
	void add_layer(
		const char* track_id,
		const char* image_name,
		int enabled_mode,
		float lifetime_seconds,
		float color_r, float color_g, float color_b,
		float opacity,
		float size_px,
		bool fade_out, float fade_out_speed,
		bool taper,
		bool flip_h, bool flip_v, bool force_right_side_up,
		float offset_x, float offset_y, bool invert_offset);

	// Record one sample for the named track. Pos/vel are in world
	// coords; dt is seconds since the last record_sample call for this
	// track. `boosting` bypasses the ALWAYS-layer speed gate so trails
	// fire instantly on boost. Tracks with no registered layers no-op
	// silently — call add_layer first if you want samples to land.
	void record_sample(const char* track_id,
		emu::vector pos, emu::vector vel,
		float dt_seconds, bool boosting);

	// Issues one glDrawArrays per visible strip per layer per visible
	// track. Uses its own shader + VBO — does NOT batch through
	// draw::flush_frame.
	//
	// `current_abs_vx` is preserved for compatibility with the prior
	// API but is currently unused — visibility was decided at
	// record_sample time and samples age out on their own clock.
	void draw_all(const draw::camera& cam, float current_abs_vx);
}

#endif
