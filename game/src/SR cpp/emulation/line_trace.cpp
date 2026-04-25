#include "line_trace.h"

using namespace emu;

line_trace::line_trace(vector start, vector end) :
	m_start{ start }, m_end{ end }
{

}

void line_trace::set(vector start, vector end)
{
	m_start = start;
	m_end = end;
}

shape line_trace::get_id() const
{
	return shape_line_trace;
}

std::unique_ptr<i_clonable> line_trace::clone() const
{
	return std::make_unique<line_trace>(m_start, m_end);
}

int32_t line_trace::get_vertex_count() const
{
	return 2;
}

vector line_trace::get_vertex(int32_t index) const
{
	if (index == 0)
		return m_start;
	if (index == 1)
		return m_end;
	return { };
}

void line_trace::get_vertices(std::vector<vector>& vertices) const
{
	if (vertices.size() < 2)
		vertices.resize(2);
	vertices[0] = m_start;
	vertices[1] = m_end;
}

vector line_trace::get_position() const
{
	return m_start;
}

void line_trace::set_position(vector pos)
{
	m_end = m_end + pos - m_start;
	m_start = pos;
}

vector line_trace::get_center() const
{
	return m_start + (m_end - m_start) / 2.0f;
}

vector line_trace::get_pivot() const
{
	return m_start;
}

void line_trace::set_pivot(vector pivot)
{

}

float line_trace::get_rotation() const
{
	return 0.0f;
}

void line_trace::set_rotation(float rotation)
{

}