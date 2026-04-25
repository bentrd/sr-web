#ifndef ENCRYPTED_FLOAT_H
#define ENCRYPTED_FLOAT_H

#include "common.h"

namespace emu
{
	struct encrypted_float
	{
		constexpr static uint32_t KEY = 0xB2C690A5u;

		uint32_t value_;

		encrypted_float();
		encrypted_float(float value);

		float get();
		void set(float value);
	};
}

#endif
