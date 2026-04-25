#include "command.h"

using namespace cmd;

std::string_view cmd::extract_part(std::string_view& expr)
{
	std::size_t begin = 0;
	while (begin < expr.size() && expr[begin] == ' ')
		begin++;

	if (begin == expr.size())
		return "";

	std::size_t end = begin;
	std::size_t expr_start = 0;
	if (expr[begin] != '"')
	{
		while (end < expr.size() && expr[end] != ' ')
			end++;
		expr_start = end;
	}
	else
	{
		begin++;
		while (end < expr.size() && expr[end] != '"')
			end++;
		expr_start = end + 1;
	}

	std::string_view part = expr.substr(begin, end - begin);
	expr = expr.substr(expr_start);
	return part;
}