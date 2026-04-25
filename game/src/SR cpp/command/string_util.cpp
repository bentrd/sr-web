#include <stdexcept>
#include <algorithm>
#include <charconv>
#include <format>
#include <ranges>

#include "string_util.h"

char to_lower(char input)
{
	if (input >= 'A' && input <= 'Z')
		input += 'a' - 'A';
	return input;
}

bool equal_lowercase(std::string_view left, std::string_view right)
{
	return std::ranges::equal(left, right, std::ranges::equal_to{}, to_lower, to_lower);
}

std::string to_string_basic(const std::uint32_t& value)
{
	return std::to_string(value);
}

std::string to_string_basic(const std::int32_t& value)
{
	return std::to_string(value);
}

std::string to_string_basic(const float& value)
{
	return std::to_string(value);
}

std::string to_string_basic(const bool& value)
{
	if (value)
		return "TRUE";
	else
		return "FALSE";
}

std::string to_string_basic(const std::string& value)
{
	return value;
}

template <std::same_as<std::uint32_t> T>
T from_string_basic(std::string_view string)
{
	for (size_t i = 0; i < string.size(); i++)
	{
		if (string[i] > '9' || string[i] < '0')
			throw std::invalid_argument(std::format("Argument \"{}\" has to be a valid unsigned integer!", string));
	}

	std::uint32_t value;
	std::from_chars(string.data(), string.data() + string.size(), value);
	return value;
}

template <std::same_as<std::int32_t> T>
T from_string_basic(std::string_view string)
{
	for (size_t i = 0; i < string.size(); i++)
	{
		if (i == 0 && string[i] == '-')
			continue;
		if (string[i] > '9' || string[i] < '0')
			throw std::invalid_argument(std::format("Argument \"{}\" has to be a valid signed integer!", string));
	}

	std::int32_t value;
	std::from_chars(string.data(), string.data() + string.size(), value);
	return value;
}

template <std::same_as<float> T>
T from_string_basic(std::string_view string)
{
	bool has_decimal_point = false;

	for (size_t i = 0; i < string.size(); i++)
	{
		bool invalid = false;
		if (i == 0 && string[i] == '-')
			continue;
		if (string[i] == '.')
		{
			if (i == 0 || i == string.size() - 1)
				invalid = true;
			if (has_decimal_point)
				invalid = true;
			has_decimal_point = true;
		}
		else if (string[i] > '9' || string[i] < '0')
			invalid = true;

		if (invalid)
			throw std::invalid_argument(std::format("Argument \"{}\" has to be a valid decimal!", string));
	}

	// std::from_chars(float) is gated to macOS 26+ in libc++; std::stof
	// works everywhere. The pre-validation above catches invalid input
	// before this line, so any throw here is a programmer error.
	return std::stof(std::string(string));
}

template <std::same_as<bool> T>
T from_string_basic(std::string_view string)
{
	if (equal_lowercase(string, "true"))
		return true;
	if (equal_lowercase(string, "t"))
		return true;

	if (equal_lowercase(string, "false"))
		return false;
	if (equal_lowercase(string, "f"))
		return false;

	throw std::invalid_argument(std::format("Argument \"{}\" has to be a valid boolean!", string));
}

template <std::same_as<std::string> T>
T from_string_basic(std::string_view string)
{
	return std::string{ string };
}

template std::uint32_t from_string_basic(std::string_view);
template std::int32_t from_string_basic(std::string_view);
template float from_string_basic(std::string_view);
template bool from_string_basic(std::string_view);
template std::string from_string_basic(std::string_view);