#include <cstdint>

#include "sr_math.h"

using namespace emu;

float emu::lerp(float val1, float val2, float am)
{
	return val1 + (val2 - val1) * am;
}

double emu::mod(double x, double y)
{
	return x - y * static_cast<uint64_t>(x / y); // probably a little inaccurate
}