#include "camera.h"

using namespace draw;

void camera::update(emu::timespan delta, emu::vector focus_point)
{
	emu::vector offset = viewport_size / 2.0f;
	position += offset;

	emu::vector velocity = focus_point - position;

	position += velocity * 5.0f * delta.seconds_f();
	position -= offset;
}