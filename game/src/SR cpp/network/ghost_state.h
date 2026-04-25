#ifndef SR_NETWORK_GHOST_STATE_H
#define SR_NETWORK_GHOST_STATE_H

#include <cstdint>
#include <string>

#include "../emulation/vector.h"

namespace net
{
	// Render-only snapshot of a remote player.
	//
	// Ghosts never enter the collision world or `state.m_inputs` — they
	// are visual entities composited at 50% alpha after the local player
	// is drawn. See AGENTS.md "Ghosts are render-only" for the rule.
	struct ghost_state
	{
		// Identity (set once via sr_set_ghost_identity, then reused).
		std::string name;
		float color_r = 1.0f;
		float color_g = 0.0f;
		float color_b = 0.0f;

		// Per-frame physics-shaped data — pushed every snapshot
		// (sr_push_ghost). Used for rendering; we do not extrapolate or
		// reconcile.
		emu::vector position{ 0, 0 };
		emu::vector velocity{ 0, 0 };
		emu::vector size{ 25.0f, 25.0f };  // matches local player default
		std::int8_t facing = 1;
		std::uint8_t anim = 0;

		// Grapple visualization.
		bool grapple_active = false;
		emu::vector grapple_origin{ 0, 0 };
		emu::vector grapple_attach{ 0, 0 };
		float grapple_length = 0.0f;
		bool grapple_taut = false;
	};
}

#endif
