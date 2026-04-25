#include "collision_pair.h"

using namespace emu;

collision_pair::collision_pair() :
	m_source{ nullptr }, m_target{ nullptr }, m_is_colliding{ false }, m_iteration{ 0 }
{

}

collision_pair::collision_pair(i_collidable* source, i_collidable* target, vector source_pos, vector source_vel, vector target_pos, vector target_vel) :
	m_source{ source }, 
	m_target(target), 
	m_source_pos{ source_pos }, 
	m_source_vel{ source_vel },
	m_target_pos{ target_pos },
	m_target_vel{ target_vel },
	m_is_colliding{ false }, 
	m_iteration{ 0 }
{

}

void collision_pair::find_blocking_axis_on_target(vector& vertex0, vector& vertex1)
{
	int32_t num = 0;
	vector vec = vec_zero;
	if (m_is_colliding)
		vec = m_mtd.normalized();
	else
		vec = (m_source->get_collision()->get_center() - m_target->get_collision()->get_center()).normalized();

	i_collision_shape* collision = m_target->get_collision();
	vector center = collision->get_center();
	float num2 = -3.4028235E+38f;
	for (int32_t i = 0; i < collision->get_vertex_count(); i++)
	{
		vector vertex2 = collision->get_vertex(i);
		vector value = vertex2 - center;
		float num3 = vec.dot(value);
		if (num3 > 0.0f)
		{
			vector vec2 = num3 * vec;
			if (vec2.length_sqr() > num2)
			{
				num2 = vec.length_sqr();
				num = i;
				vertex0.x = vertex2.x;
				vertex0.y = vertex2.y;
			}
		}
	}
	vector vertex3 = collision->get_vertex((num + 1) % collision->get_vertex_count());
	vector vertex4 = collision->get_vertex((num + collision->get_vertex_count() - 1) % collision->get_vertex_count());
	float value2 = vec.dot((vertex3 - vertex0).normalized());
	float value3 = vec.dot((vertex4 - vertex0).normalized());
	if (std::abs(value2) < std::abs(value3))
	{
		vertex1.x = vertex3.x;
		vertex1.y = vertex3.y;
		return;
	}
	vertex1.x = vertex4.x;
	vertex1.y = vertex4.y;
}