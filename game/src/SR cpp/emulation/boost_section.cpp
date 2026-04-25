#include "boost_section.h"
#include "level.h"

using namespace emu;

#ifdef OPTIMIZE_COLLISION
static bool is_aabb(boost_section::rotation rotation)
{
	return
		rotation == boost_section::DEGREE_0 ||
		rotation == boost_section::DEGREE_90 ||
		rotation == boost_section::DEGREE_180 ||
		rotation == boost_section::DEGREE_MINUS_90;
}
#endif

boost_section::boost_section()
{
	de.collision_filter = collision_filter{ 0x10000, 0x0 };
	de.unknown8 = false;

	m_rotation = DEGREE_0;

#ifdef OPTIMIZE_COLLISION
	std::construct_at(std::launder(&m_aabb));
#endif
}

boost_section::boost_section(const level_actor& def)
{
	de.collision_filter = collision_filter{ 0x10000, 0x0 };
	de.unknown8 = false;

	const std::string* type = def.get("Rotation");
	if (type == nullptr || *type == "0")
		m_rotation = DEGREE_0;
	else if (*type == "45")
		m_rotation = DEGREE_45;
	else if (*type == "90")
		m_rotation = DEGREE_90;
	else if (*type == "135")
		m_rotation = DEGREE_135;
	else if (*type == "180")
		m_rotation = DEGREE_180;
	else if (*type == "-135")
		m_rotation = DEGREE_MINUS_135;
	else if (*type == "-90")
		m_rotation = DEGREE_MINUS_90;
	else if (*type == "-45")
		m_rotation = DEGREE_MINUS_45;
	else
		m_rotation = DEGREE_0;

#ifdef OPTIMIZE_COLLISION
	if (is_aabb(m_rotation))
		std::construct_at(std::launder(&m_aabb));
	else
		std::construct_at(std::launder(&m_polygon));
#endif
}

#ifdef OPTIMIZE_COLLISION
boost_section::boost_section(const boost_section& right) :
	editable_actor{ right },
	m_rotation{ right.m_rotation }
{
	if (is_aabb(m_rotation))
		std::construct_at(std::launder(&m_aabb), right.m_aabb);
	else
		std::construct_at(std::launder(&m_polygon), right.m_polygon);
}

boost_section::~boost_section()
{
	if (is_aabb(m_rotation))
		std::destroy_at(&m_aabb);
	else
		std::destroy_at(&m_polygon);
}

boost_section& boost_section::operator=(const boost_section& right)
{
	if (is_aabb(m_rotation))
		std::destroy_at(&m_aabb);
	else
		std::destroy_at(&m_polygon);

	m_rotation = right.m_rotation;

	if (is_aabb(m_rotation))
		std::construct_at(std::launder(&m_aabb), right.m_aabb);
	else
		std::construct_at(std::launder(&m_polygon), right.m_polygon);

	return *this;
}
#endif

std::unique_ptr<i_clonable> boost_section::clone() const
{
	return std::make_unique<boost_section>(*this);
}

bool boost_section::set(const i_actor_controller* other)
{
	if (const boost_section* ptr_cast = dynamic_cast<const boost_section*>(other))
	{
		*this = *ptr_cast;
		return true;
	}
	return false;
}

void boost_section::replace_pointers(const std::map<const i_actor_controller*, i_actor_controller*>& map)
{

}

actor_init_params boost_section::get_actor_params()
{
	return
	{
		.size = vector{ 175.0f, 64.0f },
		.auto_col_det = false,
		.is_col = true,
		.should_pred_col = false,
#ifdef OPTIMIZE_COLLISION
		.has_update = false,
		.is_movable = false
#endif
	};
}

i_collision_shape* boost_section::get_collision()
{
#ifdef OPTIMIZE_COLLISION
	if (is_aabb(m_rotation))
		return &m_aabb;
	else
		return &m_polygon;
#else
	return &m_polygon;
#endif
}

collidable_type boost_section::get_collidable_type() const
{
	return col_boost_section;
}

void boost_section::init()
{
	editable_actor::init();

#ifdef OPTIMIZE_COLLISION
	switch (m_rotation)
	{
	case DEGREE_0:
	case DEGREE_180:
		m_aabb = aabb{ 0.0f, 175.0f, 0.0f, 64.0f };
		m_aabb.set_position(m_actor->d.position);
		m_actor->set_bounds_from_shape(m_aabb);
		break;
	case DEGREE_90:
	case DEGREE_MINUS_90:
		m_aabb = aabb{ 55.5f, 119.5f, -55.5f, 119.5f };
		m_aabb.set_position(m_actor->d.position);
		m_actor->set_bounds_from_shape(m_aabb);
		break;
	default:
#endif
		float rotation = (float)((double)(float)(45 * m_rotation) * 0.017453292519943295);

		m_polygon = convex_polygon{ 
			m_actor->d.position, 
			m_actor->d.size / 2, 
			rotation,
			{ { 0.0f, 0.0f }, { 175.0f, 0.0f }, { 175.0f, 64.0f }, { 0.0f, 64.0f } }
		};
		m_actor->set_bounds_from_shape(m_polygon);
#ifdef OPTIMIZE_COLLISION
	}
#endif

	m_bounds = m_actor->m_bounds;
}
