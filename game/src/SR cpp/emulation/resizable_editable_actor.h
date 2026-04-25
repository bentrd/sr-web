#ifndef RESIZABLE_EDITABLE_ACTOR
#define RESIZABLE_EDITABLE_ACTOR

#include "editable_actor.h"

namespace emu
{
	struct resizable_editable_actor : public editable_actor
	{
		struct
		{
			bool unknown1{ 0 };
			bool unknown2{ 0 };
			bool unknown3{ 0 };
			bool unknown4{ 0 };
			vector unknown5{ 0, 0 };
			vector unknown6{ 0, 0 };
		} dr;
	};
}

#endif