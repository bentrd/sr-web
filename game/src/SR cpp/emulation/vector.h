#ifndef VECTOR_H
#define VECTOR_H

#include "common.h"

namespace emu
{
	struct vector
	{
		float x;
		float y;

		inline vector() {};

		constexpr vector(float x, float y) :
			x{ x }, y{ y } {}

		constexpr bool operator==(const vector&) const = default;

		float length() const;
		float length_sqr() const;
		vector normalized() const;
		float dot(const vector& right) const;

		vector operator+(const vector& right) const;
		vector operator-(const vector& right) const;
		vector operator-() const;
		vector operator*(float factor) const;
		vector operator/(float div) const;

		vector& operator+=(const vector& right);
		vector& operator-=(const vector& right);
		vector& operator*=(float factor);
		vector& operator/=(float div);
	};

	vector operator*(float factor, const vector& vec);

	constexpr vector vec_zero{ 0.0f, 0.0f };
	constexpr vector vec_up{ 0.0f, -1.0f };
	constexpr vector vec_down{ 0.0f, 1.0f };
	constexpr vector vec_left{ -1.0f, 0.0f };
	constexpr vector vec_right{ 1.0f, 0.0f };
	inline const vector vec_up_left = vector{ -1.0f, -1.0f }.normalized();
	inline const vector vec_up_right = vector{ 1.0f, -1.0f }.normalized();
	inline const vector vec_down_left = vector{ -1.0f, 1.0f }.normalized();
	inline const vector vec_down_right = vector{ 1.0f, 1.0f }.normalized();
}

#endif
