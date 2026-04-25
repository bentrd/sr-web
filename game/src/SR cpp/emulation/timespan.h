#ifndef TIMESPAN_H
#define TIMESPAN_H

#include "common.h"

namespace emu
{
	struct timespan
	{
		constexpr static uint64_t TICKS_PER_SEC = 10000000ull;

		uint64_t ticks;

		timespan();
		constexpr timespan(uint64_t ticks) :
			ticks{ ticks } {}
		explicit timespan(float seconds);

		operator uint64_t() const;

		timespan operator+(const timespan& right) const;
		timespan operator-(const timespan& right) const;

		timespan& operator+=(const timespan& right);
		timespan& operator-=(const timespan& right);

		double seconds() const;
		float seconds_f() const;
	};
}

#endif
