#include "super_boost_volume.h"
#include "level.h"

using namespace emu;

super_boost_volume::super_boost_volume()
{
	de.collision_filter = collision_filter{ 0x1000, 0x0 };

	m_direction = LEFT;
}

super_boost_volume::super_boost_volume(const level_actor& def)
{
	de.collision_filter = collision_filter{ 0x1000, 0x0 };

	const std::string* type = def.get("Type");
	if (*type == "LEFT")
		m_direction = LEFT;
	else if (*type == "LEFT_UP")
		m_direction = LEFT_UP;
	else if (*type == "UP")
		m_direction = UP;
	else if (*type == "RIGHT_UP")
		m_direction = RIGHT_UP;
	else if (*type == "RIGHT")
		m_direction = RIGHT;
	else if (*type == "RIGHT_DOWN")
		m_direction = RIGHT_DOWN;
	else if (*type == "DOWN")
		m_direction = DOWN;
	else if (*type == "LEFT_DOWN")
		m_direction = LEFT_DOWN;
	else
		m_direction = LEFT;
}

std::unique_ptr<i_clonable> super_boost_volume::clone() const
{
	return std::make_unique<super_boost_volume>(*this);
}

bool super_boost_volume::set(const i_actor_controller* other)
{
	if (const super_boost_volume* ptr_cast = dynamic_cast<const super_boost_volume*>(other))
	{
		*this = *ptr_cast;
		return true;
	}
	return false;
}

void super_boost_volume::replace_pointers(const std::map<const i_actor_controller*, i_actor_controller*>& map)
{

}

actor_init_params super_boost_volume::get_actor_params()
{
	return
	{
		.size = vector{ 50.0f, 50.0f },
		.auto_col_det = false,
		.is_col = true,
		.should_pred_col = false,
#ifdef OPTIMIZE_COLLISION
		.has_update = false,
		.is_movable = false
#endif
	};
}

collidable_type super_boost_volume::get_collidable_type() const
{
	return col_super_boost;
}

void super_boost_volume::init()
{
	resizable_editable_actor::init();

	m_bounds.set_from_position_size(m_actor->d.position, m_actor->d.size);
	m_actor->m_bounds.set_from_position_size(m_actor->d.position, m_actor->d.size);
}

vector super_boost_volume::get_direction_vector() const
{
	switch (m_direction)
	{
		// diagonal values are rounded by the game too

	case LEFT:
		return { -1.0f, 0.0f };
	case LEFT_UP:
		return { -0.707f, -0.707f };
	case UP:
		return { 0.0f, -1.0f };
	case RIGHT_UP:
		return { 0.707f, -0.707f };
	case RIGHT:
		return { 1.0f, 0.0f };
	case RIGHT_DOWN:
		return { 0.707f, 0.707f };
	case DOWN:
		return { 0.0f, 1.0f };
	case LEFT_DOWN:
		return { -0.707f, 0.707f };
	}
	return vec_zero;
}
