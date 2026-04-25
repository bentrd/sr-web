#include <bit>

#include "encrypted_float.h"

using namespace emu;

encrypted_float::encrypted_float() {}

encrypted_float::encrypted_float(float value)
{
	value_ = std::bit_cast<uint32_t, float>(value) ^ KEY;
}

float encrypted_float::get()
{
	return std::bit_cast<float, uint32_t>(value_ ^ KEY);
}

void encrypted_float::set(float value)
{
	value_ = std::bit_cast<uint32_t, float>(value) ^ KEY;
}