#include <cmath>

#include "vector.h"

using namespace emu;

float vector::length() const
{
	return (float)std::sqrt((double)length_sqr());
}

float vector::length_sqr() const
{
	return x * x + y * y;
}

vector vector::normalized() const
{
	if (x == 0 && y == 0)
		return vector(0.0f, 0.0f);

	float length_sqr = this->length_sqr();
	float length_rec = 1.0f / (float)std::sqrt((double)length_sqr);

	return *this * length_rec;
}

float vector::dot(const vector& right) const
{
	return x * right.x + y * right.y;
}

vector vector::operator+(const vector& right) const
{
	return { x + right.x, y + right.y };
}

vector vector::operator-(const vector& right) const
{
	return { x - right.x, y - right.y };
}

vector vector::operator-() const
{
	return { -x, -y };
}

vector vector::operator*(float factor) const
{
	return { x * factor, y * factor };
}

vector vector::operator/(float div) const
{
	return { x / div, y / div };
}

vector& vector::operator+=(const vector& right)
{
	x += right.x;
	y += right.y;
	return *this;
}

vector& vector::operator-=(const vector& right)
{
	x -= right.x;
	y -= right.y;
	return *this;
}

vector& vector::operator*=(float factor)
{
	x *= factor;
	y *= factor;
	return *this;
}

vector& vector::operator/=(float div)
{
	x /= div;
	y /= div;
	return *this;
}

vector emu::operator*(float factor, const vector& vec)
{
	return { factor * vec.x, factor * vec.y };
}