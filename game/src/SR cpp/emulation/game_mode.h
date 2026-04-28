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
	time_challenge = 3,
};

// Procedural-corridor parameters shared by load_time_challenge() in the
// browser sim and the server-side replay binary. Keep them in lockstep —
// the validator must reproduce the exact same level the recording was
// made on. Width is in tiles (16 wu each), so 1875 × 16 = 30,000 wu.
namespace time_challenge
{
	inline constexpr int corridor_width_tiles = 938;    // 15,008 wu (~half of original 30k)
	inline constexpr int corridor_height_tiles = 50;
	inline constexpr int ceil_y = 2;
	inline constexpr int floor_y = 23;
	inline constexpr int start_x = 1;                    // column 1 (next to left wall)

	// X threshold the player's right edge must cross to end a run. The
	// right wall sits at column (width - 1), so its left edge is at
	// (width - 1) * 16 = 14,992. We trigger one wu inside that to
	// absorb sub-pixel collision resolution.
	inline constexpr float end_x_threshold = (corridor_width_tiles - 1) * 16.0f - 1.0f; // 14,991
}

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