#include "timespan.h"

using namespace emu;

timespan::timespan() :
	ticks{ 0 } {}

timespan::timespan(float seconds) :
	ticks{ (uint64_t)(seconds * 1E07) } {}

timespan::operator uint64_t() const
{
	return ticks;
}

timespan timespan::operator+(const timespan& right) const
{
	return ticks + right.ticks;
}

timespan timespan::operator-(const timespan& right) const
{
	return ticks - right.ticks;
}

timespan& timespan::operator+=(const timespan& right)
{
	ticks += right.ticks;
	return *this;
}

timespan& timespan::operator-=(const timespan& right)
{
	ticks -= right.ticks;
	return *this;
}

double timespan::seconds() const
{
	return ticks * 1E-07;
}

float timespan::seconds_f() const
{
	return ticks * 1E-07f;
}