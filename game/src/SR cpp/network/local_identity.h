#ifndef SR_NETWORK_LOCAL_IDENTITY_H
#define SR_NETWORK_LOCAL_IDENTITY_H

#include <string>

namespace net
{
	// The local player's display name + color, set once from JS via
	// sr_set_local_identity. Used by the renderer to override the
	// hardcoded red player rectangle and by sr_get_player_screen_pos
	// when called with id == "" (local).
	struct local_identity
	{
		std::string name;
		float r = 1.0f;
		float g = 0.0f;
		float b = 0.0f;
		bool is_set = false;
	};
}

#endif
