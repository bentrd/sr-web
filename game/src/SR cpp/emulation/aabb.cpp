#include "aabb.h"
#include "common.h"

using namespace emu;

aabb::aabb() :
	min_x{ 0.0f }, max_x{ 0.0f }, 
	min_y{ 0.0f }, max_y{ 0.0f }, 
	width{ 0.0f }, height{ 0.0f }
{

}

aabb::aabb(float min_x, float max_x, float min_y, float max_y) :
	min_x{ min_x }, max_x{ max_x },
	min_y{ min_y }, max_y{ max_y },
	width{ max_x - min_x }, height{ max_y - min_y }
{

}

shape aabb::get_id() const
{
	return shape_aabb;
}

std::unique_ptr<i_clonable> aabb::clone() const
{
	return std::make_unique<aabb>(min_x, max_x, min_y, max_y);
}

int32_t aabb::get_vertex_count() const
{
	return 4;
}

vector aabb::get_vertex(int32_t index) const
{
	switch (index)
	{
	case 0:
		return vector{ max_x, min_y };
	case 1:
		return vector{ min_x, min_y };
	case 2:
		return vector{ min_x, max_y };
	case 3:
		return vector{ max_x, max_y };
	}
	return vector{ 0.0f, 0.0f };
}

void aabb::get_vertices(std::vector<vector>& vertices) const
{
	if (vertices.size() < 4)
		vertices.resize(4);
	vertices[0] = vector{ max_x, min_y };
	vertices[1] = vector{ min_x, min_y };
	vertices[2] = vector{ min_x, max_y };
	vertices[3] = vector{ max_x, max_y };
}

vector aabb::get_position() const
{
	return vector{ min_x, min_y };
}

void aabb::set_position(vector position)
{
	min_x = position.x;
	max_x = position.x + width;
	min_y = position.y;
	max_y = position.y + height;
}

vector aabb::get_center() const
{
	return vector{ min_x + width / 2.0f, min_y + height / 2.0f };
}

vector aabb::get_pivot() const
{
	return get_center() - get_position();
}

void aabb::set_pivot(vector pivot)
{

}

float aabb::get_rotation() const
{
	return 0.0f;
}

void aabb::set_rotation(float rotation)
{

}

void aabb::set_from_position_size(vector position, vector size)
{
	width = size.x;
	height = size.y;
	min_x = position.x;
	min_y = position.y;
	max_x = position.x + size.x;
	max_y = position.y + size.y;
}

void aabb::set_from_shape(i_collision_shape& shape)
{
	min_x = FLOAT_MAX;
	max_x = FLOAT_MIN;
	min_y = FLOAT_MAX;
	max_y = FLOAT_MIN;
	for (int32_t i = 0; i < shape.get_vertex_count(); i++)
	{
		vector vertex = shape.get_vertex(i);
		min_x = std::min(min_x, vertex.x);
		max_x = std::max(max_x, vertex.x);
		min_y = std::min(min_y, vertex.y);
		max_y = std::max(max_y, vertex.y);
	}
	width = max_x - min_x;
	height = max_y - min_y;
}

bool aabb::overlaps(const aabb& other) const
{
	return min_x < other.max_x && max_x > other.min_x && min_y < other.max_y && max_y > other.min_y;
}

bool aabb::contains(vector point) const
{
	return min_x < point.x && max_x > point.x && min_y < point.y && max_y > point.y;
}