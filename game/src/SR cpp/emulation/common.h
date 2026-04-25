#ifndef COMMON_H
#define COMMON_H

#include <memory>
#include <cstdint>

#include "config.h"

namespace emu
{
	using uint8_t = std::uint8_t;
	using int8_t = std::int8_t;
	using uint16_t = std::uint16_t;
	using int16_t = std::int16_t;
	using uint32_t = std::uint32_t;
	using int32_t = std::int32_t;
	using uint64_t = std::uint64_t;
	using int64_t = std::int64_t;
	using size_t = std::size_t;

	struct i_clonable
	{
		virtual std::unique_ptr<i_clonable> clone() const = 0;
		virtual ~i_clonable() = default;
	};

	template <std::derived_from<i_clonable> T>
	std::unique_ptr<T> clone(const i_clonable* to_clone)
	{
		return std::unique_ptr<T>{ static_cast<T*>(to_clone->clone().release()) };
	}

	constexpr float FLOAT_MAX = 3.4028235E+38f;
	constexpr float FLOAT_MIN = -3.4028235E+38f;
}

#endif