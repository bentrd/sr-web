#ifndef GAME_MODE_H
#define GAME_MODE_H

#include "timespan.h"

namespace emu
{

// Mirrors the TypeScript GameMode union in packages/protocol/src/index.ts.
// Add new values here, then add the string label in protocol, then gate
// mode-specific logic in playground::update with a case branch.
enum class GameMode : int
{
	standard = 0,
	grapple_challenge = 1,
	rg_challenge = 2,
};

// Per-session state for the RG Challenge mode.
// Reset by playground::reset_rg_state().
struct RgChallengeState
{
	// --- Leaderboard-visible stats ---
	int consecutive = 0;       // current streak (displayed in HUD)
	int session_best = 0;      // best streak this session

	// --- Detection state (transient, reset per grapple cycle) ---
	bool grapple_was_thrown_left = false;  // true if last shot had direction.x < 0
	timespan grapple_connect_time{0ull};      // sim time when grapple connected
	bool was_swinging_prev = false;        // previous frame's is_swinging
	bool was_grappling_prev = false;       // previous frame's is_grappling

	// --- Floor/ceiling tracking ---
	bool was_on_ground_prev = false;       // for detecting touchdown
	bool was_ceiling_hit_prev = false;     // for detecting ceiling bonk

	// How many sim steps since last RG streak reset (informational).
	int steps_since_reset = 0;

	void reset_streak()
	{
		consecutive = 0;
		steps_since_reset = 0;
		grapple_was_thrown_left = false;
		grapple_connect_time = timespan{0ull};
	}
};

} // namespace emu

#endif