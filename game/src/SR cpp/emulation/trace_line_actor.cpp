#include "trace_line_actor.h"

using namespace emu;

trace_line_actor::trace_line_actor() : 
	m_collision{ vector{ 0.0f, 0.0f }, vector{ 0.0f, 0.0f } }
{
	
}

trace_line_actor::trace_line_actor(vector start, vector end) :
	m_collision{ start, end }
{
	m_bounds = aabb{ std::min(start.x, end.x), std::max(start.x, end.x), std::min(start.y, end.y), std::max(start.y, end.y) };
}

trace_line_actor::trace_line_actor(vector start, vector end, collision_filter filter) :
	m_collision{ start, end }, m_collision_filter{ filter }
{
	m_bounds = aabb{ std::min(start.x, end.x), std::max(start.x, end.x), std::min(start.y, end.y), std::max(start.y, end.y) };
}

std::unique_ptr<i_clonable> trace_line_actor::clone() const
{
	return nullptr;
}

void trace_line_actor::set(vector start, vector end, collision_filter filter)
{
	m_collision = line_trace{ start, end };
	m_bounds = aabb{ std::min(start.x, end.x), std::max(start.x, end.x), std::min(start.y, end.y), std::max(start.y, end.y) };
	m_collision_filter = filter;
}
	
vector trace_line_actor::get_position() const
{
	return m_bounds.get_center();
}

vector trace_line_actor::get_velocity() const
{
	return vector{ 0.0f, 0.0f };
}

const aabb& trace_line_actor::get_bounds() const
{
	return m_bounds;
}

i_collision_shape* trace_line_actor::get_collision()
{
	return &m_collision;
}

collision_filter& trace_line_actor::get_collision_filter()
{
	return m_collision_filter;
}

collidable_type trace_line_actor::get_collidable_type() const
{
	return col_invalid;
}