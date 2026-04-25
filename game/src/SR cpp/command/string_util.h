#ifndef STRING_UTIL
#define STRING_UTIL

#include <string>

char to_lower(char input);
bool equal_lowercase(std::string_view left, std::string_view right);

template <typename T, typename... Ts>
constexpr bool is_same_any = ((std::is_same_v<T, Ts>) || ...);

template <typename T>
constexpr bool is_basic_to_string = is_same_any<T, std::uint32_t, std::int32_t, float, bool, std::string>;

std::string to_string_basic(const std::uint32_t& value);
std::string to_string_basic(const std::int32_t& value);
std::string to_string_basic(const float& value);
std::string to_string_basic(const bool& value);
std::string to_string_basic(const std::string& value);

template <typename T>
std::string to_string(const T& value)
{
	if constexpr (requires { to_string_basic(value); })
		return to_string_basic(value);
	else
		return T::to_string(value);
}

template <std::same_as<std::uint32_t> T>
T from_string_basic(std::string_view string);
template <std::same_as<std::int32_t> T>
T from_string_basic(std::string_view string);
template <std::same_as<float> T>
T from_string_basic(std::string_view string);
template <std::same_as<bool> T>
T from_string_basic(std::string_view string);
template <std::same_as<std::string> T>
T from_string_basic(std::string_view string);

template <typename T>
T from_string(std::string_view string)
{
	if constexpr (requires { from_string_basic<T>(string); })
		return from_string_basic<T>(string);
	else
		return T::from_string(string);
}

#endif
