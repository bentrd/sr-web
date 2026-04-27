#pragma once

// Runtime-tunable rendering palette. The web build's OptionsModal pokes
// these via the sr_set_visual_* C ABI exports; they're consumed by
// instance::draw() (background) and draw_util.cpp (tile + grapple
// colors / sizes). Defaults match the original hardcoded palette so a
// fresh client looks identical to the launch build.

namespace draw
{
	struct visuals_config
	{
		// Play-area clear color. Read every frame so a slider feels live.
		float bg_r = 0.16f, bg_g = 0.17f, bg_b = 0.20f;

		// Tile body — the medium gray on solid tiles + the base under the
		// stripe on grappable / climbable tiles.
		float walls_r = 0.62f, walls_g = 0.64f, walls_b = 0.68f;

		// Stripe on tile_grapple_ceil ("grapple this surface" affordance).
		float grapple_stripe_r = 1.0f, grapple_stripe_g = 1.0f, grapple_stripe_b = 1.0f;

		// Stripe on tile_wall_left / tile_wall_right ("wallclimb" affordance).
		float wallclimb_stripe_r = 1.0f, wallclimb_stripe_g = 1.0f, wallclimb_stripe_b = 1.0f;

		// Grapple rope quad.
		float grapple_cord_r = 0.0f, grapple_cord_g = 0.0f, grapple_cord_b = 0.0f;

		// Grapple hook tip rectangle. Color + per-axis size in world units
		// (default 12x12 mirrors the actor's collision box).
		float grapple_head_r = 1.0f, grapple_head_g = 0.0f, grapple_head_b = 0.0f;
		float grapple_head_size = 12.0f;

		// Boost strip (boost_section actor) — solid green by default.
		float boost_section_r = 0.0f, boost_section_g = 1.0f, boost_section_b = 0.0f, boost_section_a = 1.0f;

		// Super-boost tinted volume (super_boost_volume actor) — 10% green
		// so it tints without obscuring the underlying tiles.
		float boost_pickup_r = 0.0f, boost_pickup_g = 1.0f, boost_pickup_b = 0.0f, boost_pickup_a = 0.1f;

		// Top-center boost meter on the local player. Hidden in challenge
		// modes (the HUD shows session-best instead and the bar overlaps
		// the leaderboard). Defaults to true to keep regular play unchanged.
		bool show_boost_bar = true;
	};

	// Single mutable instance owned by the renderer. Returned by reference
	// so call sites can read/write without going through getters.
	visuals_config& visuals();
}
