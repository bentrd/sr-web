#ifndef RG_DETECTOR_H
#define RG_DETECTOR_H

#include "game_mode.h"
#include "timespan.h"

namespace emu
{
	struct player;

	// Run RG (release-grapple) detection for one sim step. Mutates `rg`
	// in-place. Designed to be called *after* state::update() so the
	// player's flags (is_swinging, is_grappling, is_on_ground, ...) and
	// velocity reflect the current frame.
	//
	// Extracted from playground::update() so the server-side replay
	// binary can run the same detection without pulling in
	// playground/drawing/network sources.
	void update_rg_state(RgChallengeState& rg, const player& p, timespan now);
}

#endif
