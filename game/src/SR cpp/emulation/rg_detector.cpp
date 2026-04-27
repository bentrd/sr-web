#include "rg_detector.h"
#include "player.h"
#include "grapple.h"

namespace emu
{
	void update_rg_state(RgChallengeState& rg, const player& p, timespan now)
	{
		const bool is_swinging = p.d.is_swinging;
		const bool is_grappling = p.d.is_grappling;
		const bool is_on_ground = p.d.is_on_ground;
		const bool is_ceiling_hit = p.d.is_ceiling_hit;

		// 3 sim frames at 300 Hz = 3 * 33333 = 99999 ticks.
		constexpr uint64_t k_three_frame_ticks = 99999;

		// 1) Detect grapple SHOT direction (only when hook is flying).
		if (!rg.was_grappling_prev && is_grappling && !is_swinging)
		{
			if (p.m_grapple != nullptr)
			{
				rg.grapple_was_thrown_left = p.m_grapple->d.direction.x < 0.0f;
			}
		}

		// 2) Detect grapple CONNECT (transition into swinging).
		if (!rg.was_swinging_prev && is_swinging)
		{
			rg.grapple_connect_time = now;
		}

		// 3) Detect FAILED RG — still swinging with a left grapple past 3 frames.
		if (is_swinging && rg.grapple_was_thrown_left)
		{
			const uint64_t swinging_ticks = (now - rg.grapple_connect_time).ticks;
			if (swinging_ticks > k_three_frame_ticks)
			{
				rg.reset_streak();
				rg.grapple_was_thrown_left = false;
				rg.grapple_connect_time = timespan{0ull};
			}
		}

		// 4) Detect grapple RELEASE (transition out of swinging).
		if (rg.was_swinging_prev && !is_swinging && rg.grapple_was_thrown_left)
		{
			const uint64_t elapsed_ticks = (now - rg.grapple_connect_time).ticks;
			if (elapsed_ticks <= k_three_frame_ticks)
			{
				const float vx = p.m_actor->d.velocity.x;
				if (vx > 0.0f)
				{
					rg.consecutive++;
					if (rg.consecutive > rg.session_best)
						rg.session_best = rg.consecutive;
				}
			}
			rg.grapple_was_thrown_left = false;
			rg.grapple_connect_time = timespan{0ull};
		}

		// 5) Floor touch resets streak. Ceiling hits are allowed.
		const bool just_touched_ground = !rg.was_on_ground_prev && is_on_ground;
		if (just_touched_ground)
		{
			rg.reset_streak();
		}

		// Persist previous-frame state.
		rg.was_swinging_prev = is_swinging;
		rg.was_grappling_prev = is_grappling;
		rg.was_on_ground_prev = is_on_ground;
		rg.was_ceiling_hit_prev = is_ceiling_hit;
		rg.steps_since_reset++;
	}
}
