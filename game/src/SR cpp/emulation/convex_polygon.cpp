#include <cmath>

#include "convex_polygon.h"

using namespace emu;

convex_polygon::convex_polygon(std::initializer_list<vector> local_vertices) :
	m_position{ vec_zero }, 
	m_pivot{ vec_zero }, 
	m_rotation{ 0.0f },
#ifndef OPTIMIZE_COLLISION
	m_local_vertices(local_vertices.size()),
#endif
	m_vertices(local_vertices.size())
{
#ifdef OPTIMIZE_COLLISION
	std::ranges::copy(local_vertices, m_vertices.begin());
#else
	std::ranges::copy(local_vertices, m_local_vertices.begin());
#endif
	calculate_vertices();
}

convex_polygon::convex_polygon(vector position, vector pivot, float rotation, std::initializer_list<vector> local_vertices) :
	m_position{ position },
	m_pivot{ pivot },
	m_rotation{ rotation },
#ifndef OPTIMIZE_COLLISION
	m_local_vertices(local_vertices.size()),
#endif
	m_vertices(local_vertices.size())
{
#ifdef OPTIMIZE_COLLISION
	std::ranges::copy(local_vertices, m_vertices.begin());
#else
	std::ranges::copy(local_vertices, m_local_vertices.begin());
#endif
	calculate_vertices();
}

shape convex_polygon::get_id() const
{
	return shape_convex_polygon;
}

std::unique_ptr<i_clonable> convex_polygon::clone() const
{
	return std::make_unique<convex_polygon>(*this);
}

void convex_polygon::calculate_vertices()
{
	m_center = vec_zero;

#ifdef OPTIMIZE_COLLISION
	float cos = 1.0f;
	float sin = 0.0f;

	if (m_rotation != 0.0f)
	{
		cos = std::cos(m_rotation);
		sin = std::sin(m_rotation);
	}

	if (cos == 1.0f)
	{
		for (vector& vertex : m_vertices)
		{
			vertex += m_position;
			m_center += vertex;
		}
	}
	else
	{
		for (vector& vertex : m_vertices)
		{
			vector temp = vertex - m_pivot;
			vertex.x = temp.x * cos - temp.y * sin;
			vertex.y = temp.y * cos + temp.x * sin;
			vertex += m_pivot + m_position;
			m_center += vertex;
		}
	}
#else
	float cos = std::cos(m_rotation);
	float sin = std::sin(m_rotation);

	auto it = m_vertices.begin();
	for (vector vertex : m_local_vertices)
	{
		vector temp = vertex - m_pivot;
		vertex.x = temp.x * cos - temp.y * sin;
		vertex.y = temp.y * cos + temp.x * sin;
		vertex += m_pivot + m_position;
		*(it++) = vertex;
		m_center += vertex;
	}
#endif
	m_center = m_center / (float)m_vertices.size();
}

int32_t convex_polygon::get_vertex_count() const
{
	return (int32_t)m_vertices.size();
}

vector convex_polygon::get_vertex(int32_t index) const
{
	return m_vertices[index];
}

void convex_polygon::get_vertices(std::vector<vector>& vertices) const
{
	if (vertices.size() < m_vertices.size())
		vertices.resize(m_vertices.size());

	for (size_t i = 0; i < m_vertices.size(); i++)
		vertices[i] = m_vertices[i];
}

vector convex_polygon::get_position() const
{
    return m_position;
}

void convex_polygon::set_position(vector position)
{
#ifdef OPTIMIZE_COLLISION
	vector d = position - m_position;
#endif
	m_position = position;
#ifdef OPTIMIZE_COLLISION
	for (vector& vertex : m_vertices)
		vertex += d;
	m_center += d;
#else
	calculate_vertices();
#endif
}

vector convex_polygon::get_center() const
{
  	return m_center;
}

vector convex_polygon::get_pivot() const
{
	return m_pivot;
}

void convex_polygon::set_pivot(vector pivot)
{
	m_pivot = pivot;
	calculate_vertices();
}

float convex_polygon::get_rotation() const
{
	return m_rotation;
}

void convex_polygon::set_rotation(float rotation)
{
	m_rotation = rotation;
	calculate_vertices();
}

void convex_polygon::set_position_rotation(vector position, float rotation)
{
	m_position = position;
	m_rotation = rotation;
	calculate_vertices();
}