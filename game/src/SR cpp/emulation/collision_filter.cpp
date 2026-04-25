#include "collision_filter.h"

using namespace emu;

collision_filter::collision_filter() {}

bool collision_filter::collides_with(collision_filter other) const
{
	return (m_category_bits & other.m_mask_bits) > 0;
}
