#include "ghost_manager.h"

using namespace net;

void ghost_manager::push(const std::string& id,
	emu::vector position, emu::vector velocity,
	std::int8_t facing, std::uint8_t anim,
	bool grapple_active,
	emu::vector grapple_origin, emu::vector grapple_attach,
	float grapple_length, bool grapple_taut,
	emu::vector size)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto& g = m_ghosts[id];
	g.position = position;
	g.velocity = velocity;
	g.facing = facing;
	g.anim = anim;
	g.grapple_active = grapple_active;
	g.grapple_origin = grapple_origin;
	g.grapple_attach = grapple_attach;
	g.grapple_length = grapple_length;
	g.grapple_taut = grapple_taut;
	// Guard against early frames where the sender has not populated size
	// yet (e.g. PROTOCOL_VERSION 2 clients on a mismatched server, or a
	// race during sr_load_map). Keeps the previous valid size instead of
	// collapsing the ghost to (0,0).
	if (size.x > 0.0f && size.y > 0.0f) g.size = size;
}

void ghost_manager::set_identity(const std::string& id, const std::string& name,
	float r, float g, float b)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto& gh = m_ghosts[id];
	gh.name = name;
	gh.color_r = r;
	gh.color_g = g;
	gh.color_b = b;
}

void ghost_manager::remove(const std::string& id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_ghosts.erase(id);
}

void ghost_manager::clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_ghosts.clear();
}

std::unordered_map<std::string, ghost_state> ghost_manager::snapshot() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_ghosts;
}

const ghost_state* ghost_manager::try_get(
	const std::unordered_map<std::string, ghost_state>& snap,
	const std::string& id) const
{
	auto it = snap.find(id);
	return it == snap.end() ? nullptr : &it->second;
}
