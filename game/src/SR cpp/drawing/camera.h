#ifndef CAMERA_H
#define CAMERA_H

#include "../emulation/vector.h"
#include "../emulation/timespan.h"

namespace draw
{
	struct camera
	{
		emu::vector viewport_size;
		emu::vector position = emu::vec_zero;

		void update(emu::timespan delta, emu::vector focus_point);
	};
}

#endif