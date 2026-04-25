#include "obstacle.h"
#include "level.h"

using namespace emu;

obstacle::obstacle()
{
	de.collision_filter = collision_filter{ 0x800, 0x0 };
}

obstacle::obstacle(const level_actor& def)
{
	de.collision_filter = collision_filter{ 0x800, 0x0 };
}

std::unique_ptr<i_clonable> obstacle::clone() const
{
	return std::make_unique<obstacle>(*this);
}

bool obstacle::set(const i_actor_controller* other)
{
	if (const obstacle* ptr_cast = dynamic_cast<const obstacle*>(other))
	{
		*this = *ptr_cast;
		return true;
	}
	return false;
}

void obstacle::replace_pointers(const std::map<const i_actor_controller*, i_actor_controller*>& map)
{

}

actor_init_params obstacle::get_actor_params()
{
	return
	{
		.size = vector{ 16.0f, 16.0f },
		.auto_col_det = false,
		.is_col = true,
		.should_pred_col = false,
#ifdef OPTIMIZE_COLLISION
		.has_update = true,
		.is_movable = false
#endif
	};
}

collidable_type obstacle::get_collidable_type() const
{
	return col_obstacle;
}

void obstacle::init()
{
	editable_actor::init();

	m_bounds.set_from_position_size(m_actor->d.position, m_actor->d.size);
	m_actor->m_bounds.set_from_position_size(m_actor->d.position, m_actor->d.size);
}

void obstacle::reset()
{
	editable_actor::reset();

	de.collision_filter.m_category_bits = 0x800;
	d.is_broken = false;
	d.break_timer = 0.0f;
	d.break_shade = 1.0f;
}

void obstacle::update(timespan time, timespan delta)
{
	editable_actor::update(time, delta);

	if (d.is_broken)
	{
		float delta_sec = delta.seconds_f();
		d.break_velocity += vector{ 0, 1000 } * delta_sec;
		d.break_shade -= delta_sec;
		d.break_timer += delta_sec;
		if (d.break_timer > 20.0f)
			reset();
	}
}

void obstacle::break_(timespan time, vector position)
{
	d.is_broken = true;
	d.break_time = time;
	d.break_shade = 1.0f;
	d.break_timer = 0.0f;
	de.collision_filter.m_category_bits = 0;
	de.collision_filter.m_mask_bits = 0;
	d.break_velocity = (m_actor->d.position - position).normalized();
	d.break_velocity *= 200.0f;
	d.break_rotation_direction = d.break_velocity.x > 0.0f ? 1.0f : -1.0f;
}
