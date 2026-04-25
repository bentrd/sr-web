#include "trace_polygon_actor.h"
#include "collision_utils.h"

using namespace emu;

trace_polygon_actor::trace_polygon_actor()
{

}

trace_polygon_actor::trace_polygon_actor(const aabb& bounds, i_collision_shape* collision)
{
	set(bounds, collision);
}

trace_polygon_actor::trace_polygon_actor(const aabb& bounds, i_collision_shape* collision, collision_filter filter)
{
	set(bounds, collision, filter);
}

std::unique_ptr<i_clonable> trace_polygon_actor::clone() const
{
	return nullptr;
}

void trace_polygon_actor::set(const aabb& bounds, i_collision_shape* collision)
{
	m_bounds = bounds;
	collision_utils::assign_collision_shape(m_collision, collision);
	m_collision_filter = filter_all;
}

void trace_polygon_actor::set(const aabb& bounds, i_collision_shape* collision, collision_filter filter)
{
	m_bounds = bounds;
	collision_utils::assign_collision_shape(m_collision, collision);
	m_collision_filter = filter;
}

vector trace_polygon_actor::get_position() const
{
	return m_bounds.get_center();
}

vector trace_polygon_actor::get_velocity() const
{
	return vector{ 0.0f, 0.0f };
}

const aabb& trace_polygon_actor::get_bounds() const
{
	return m_bounds;
}

i_collision_shape* trace_polygon_actor::get_collision()
{
	return m_collision.get();
}

collision_filter& trace_polygon_actor::get_collision_filter()
{
	return m_collision_filter;
}

collidable_type trace_polygon_actor::get_collidable_type() const
{
	return col_invalid;
}
