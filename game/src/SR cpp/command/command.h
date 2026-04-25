#ifndef COMMAND_H
#define COMMAND_H

#include <string_view>
#include <array>
#include <stdexcept>
#include <format>
#include <string>
#include <charconv>
#include <functional>
#include <iostream>

#include "string_util.h"

struct instance;

namespace cmd
{
	std::string_view extract_part(std::string_view& expr);

	template <std::size_t N>
	std::array<std::string_view, N> split_expr(std::string_view expr)
	{
		std::array<std::string_view, N> parts;
		std::size_t i = 0;
		std::string_view cur_part;

		while (!(cur_part = extract_part(expr)).empty())
		{
			if (i < N)
				parts[i] = cur_part;
			i++;
		}

		if (i < N)
			throw std::invalid_argument(std::format("Too few arguments! Expected: {}, Provided: {}", N, i));
		if (i > N)
			throw std::invalid_argument(std::format("Too many arguments! Expected: {}, Provided: {}", N, i));

		return parts;
	}

	template <typename... Ts>
	void execute(std::string_view& args_str, const std::function<void(Ts...)>& function)
	{
		constexpr std::size_t N = sizeof...(Ts);
		if constexpr (N == 0) // to not get unused lambda capture warning
		{
			split_expr<N>(args_str);
			function();
		}
		else
		{
			std::array<std::string_view, N> parts = split_expr<N>(args_str);
			[&]<std::size_t... Is>(std::index_sequence<Is...>)
			{
				function(from_string<Ts>(parts[Is])...);
			}(std::make_index_sequence<N>{});
		}
	}

	struct command_base
	{
		virtual ~command_base() = default;
		virtual void execute(instance& inst, std::string_view args) = 0;
	};

	template <typename... Ts>
	struct command : public command_base
	{
		std::function<void(instance&, Ts...)> m_func;

		command(std::function<void(instance&, Ts...)> func) :
			m_func(func) {}

		virtual ~command() = default;

		void execute(instance& inst, std::string_view args) override
		{
			try
			{
				std::function<void(Ts...)> bound = std::bind_front(m_func, std::ref(inst));
				cmd::execute<Ts...>(args, bound);
			}
			catch (const std::exception& e)
			{
				std::cout << "ERROR: " << e.what() << "\n";
			}
		}
	};
}

#endif
