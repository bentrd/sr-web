#ifndef TRAIL_H
#define TRAIL_H

#include <cstdint>

#include "camera.h"
#include "../emulation/vector.h"

// Textured ribbon trails behind the local player. Mirrors SpeedRunners'
// trail layer system: each layer is an independent ribbon with its own
// image, sample lifetime, taper, and offset. Samples are recorded in
// world coordinates from playground::update; draw_all() renders every
// layer in submission order, projecting through the camera.
//
// Image data is owned here — each register_image() upload becomes a GL
// texture; clear() releases them. JS owns the source PNGs and the
// .trail layer config; this subsystem is pure render machinery.
namespace trail
{
	void init();
	void shutdown();

	// Drop every layer + every uploaded image. Called when JS reloads the
	// active trail (e.g. user picks a different one mid-session).
	void clear();

	// Upload a 32-bit RGBA texture and key it by `name`. Layers reference
	// images by the same name. Re-registering replaces the previous tex.
	void register_image(const char* name, int w, int h, const std::uint8_t* rgba);

	// Append a layer that draws image `image_name`. enabled_mode:
	//   0 = ALWAYS (sample every record_sample tick)
	//   1 = ONLY AT SUPERSPEED (only sample when speed >= 1200 px/s)
	// Properties match the .trail file layout 1:1 — see parseTrail.ts.
	void add_layer(
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

	// Called once per sim tick from playground::update. Pos/vel are in
	// world coords; dt is seconds since the last record_sample call.
	// Sampling is throttled to ~60 Hz internally so a 300 Hz sim doesn't
	// over-fill the buffer.
	void record_sample(emu::vector pos, emu::vector vel, float dt_seconds);

	// Issues one glDrawArrays per visible strip per layer. Uses its own
	// shader + VBO — does NOT batch through draw::flush_frame. Caller is
	// responsible for ordering this between flushes if z-order matters.
	//
	// `current_abs_vx` is the local player's |vel.x| this frame, used to
	// fade ONLY_AT_SUPERSPEED layers. Below the trigger they vanish even
	// if old samples are still inside their lifetime — so the visible
	// trail tracks the *current* player speed, not its 2-seconds-ago
	// speed when those samples were recorded.
	void draw_all(const draw::camera& cam, float current_abs_vx);
}

#endif
