#include "quad_tree_node.h"
#include "actor.h"

using namespace emu;

quad_tree_node::quad_tree_node(const aabb& bounds) :
	m_center{ bounds.get_center() }, 
	m_depth{ 0 },
	m_bounds{ bounds }
{

}

quad_tree_node::quad_tree_node(const aabb& bounds, int32_t depth) :
	m_center{ bounds.get_center() },
	m_depth{ depth },
	m_bounds{ bounds }
{

}

quad_tree_node::quad_tree_node(const quad_tree_node& right) :
	m_center{ right.m_center },
	m_depth{ right.m_depth },
	m_bounds{ right.m_bounds },
	m_leaves{ right.m_leaves }
{
	for (int32_t i = 0; i < 4; i++)
	{
		if (right.m_children[i] != nullptr)
			m_children[i] = std::make_unique<quad_tree_node>(*right.m_children[i]);
	}
}

quad_tree_node::quad_tree_node(quad_tree_node&& right) noexcept :
	m_center{ right.m_center },
	m_depth{ right.m_depth },
	m_bounds{ right.m_bounds },
	m_children{ std::move(right.m_children) },
	m_leaves{ std::move(right.m_leaves) }
{
	for (actor* leaf : m_leaves)
		leaf->set_quad_tree_parent(this);
}

quad_tree_node& quad_tree_node::operator=(const quad_tree_node& right)
{
	m_center = right.m_center;
	m_depth = right.m_depth;
	m_bounds = right.m_bounds;
	m_leaves = right.m_leaves;

	for (int32_t i = 0; i < 4; i++)
	{
		if (right.m_children[i] != nullptr)
		{
			if (m_children[i] == nullptr)
				m_children[i] = std::make_unique<quad_tree_node>(*right.m_children[i]);
			else
				*m_children[i] = *right.m_children[i];
		}
		else
			m_children[i].reset();
	}
	return *this;
}

quad_tree_node& quad_tree_node::operator=(quad_tree_node&& right) noexcept
{
	m_center = right.m_center;
	m_depth = right.m_depth;
	m_bounds = right.m_bounds;
	m_children = std::move(right.m_children);
	m_leaves = std::move(right.m_leaves);

	for (actor* leaf : m_leaves)
		leaf->set_quad_tree_parent(this);

	return *this;
}

std::unique_ptr<i_clonable> quad_tree_node::clone() const
{
	return std::make_unique<quad_tree_node>(*this);
}

void quad_tree_node::replace_pointers(const std::map<const actor*, actor*>& map)
{
	for (const auto& child : m_children)
	{
		if (child != nullptr)
			child->replace_pointers(map);
	}

	for (actor*& leave : m_leaves)
	{
		auto it = map.find(leave);
		if (it != map.end())
		{
			leave = it->second;
			leave->set_quad_tree_parent(this);
		}
	}
}

void quad_tree_node::clear()
{
	for (auto& child : m_children)
	{
		if (child.get() != nullptr)
		{
			child->clear();
			child.reset();
		}
	}

	for (actor* leaf : m_leaves)
		leaf->set_quad_tree_parent(nullptr);

	m_leaves.clear();
}

void quad_tree_node::add(actor* leaf)
{
	if (leaf->get_quad_tree_parent() == nullptr)
	{
		if (overlapping_center_axes(leaf->get_bounds()) || m_depth >= 10)
		{
			leaf->set_quad_tree_parent(this);
			m_leaves.push_back(leaf);
			return;
		}
		if (m_children.front() == nullptr)
			refresh_children();
		get_overlapping_child(leaf->get_bounds())->add(leaf);
	}
}

void quad_tree_node::remove(actor* leaf)
{
	quad_tree_node* parent = leaf->get_quad_tree_parent();
	if (parent != nullptr)
	{
		leaf->set_quad_tree_parent(nullptr);
		auto it = std::ranges::find(parent->m_leaves, leaf);
		if (it != parent->m_leaves.end())
			parent->m_leaves.erase(it);
	}
}

void quad_tree_node::update(actor* leaf)
{
	remove(leaf);
	add(leaf);
}

void quad_tree_node::refresh_children()
{
	std::array<aabb, 4> bounds
	{
		aabb{ m_bounds.min_x, m_center.x, m_bounds.min_y, m_center.y },
		aabb{ m_center.x, m_bounds.max_x, m_bounds.min_y, m_center.y },
		aabb{ m_bounds.min_x, m_center.x, m_center.y, m_bounds.max_y },
		aabb{ m_center.x, m_bounds.max_x, m_center.y, m_bounds.max_y }
	};
	
	for (int32_t i = 0; i < 4; i++)
	{
		if (m_children[i] == nullptr)
			m_children[i] = std::make_unique<quad_tree_node>(bounds[i], m_depth + 1);
		else
			*m_children[i] = quad_tree_node{ bounds[i], m_depth + 1 };
	}
}

bool quad_tree_node::overlapping_center_axes(const aabb& bounds) const
{
	return (m_center.x >= bounds.min_x && m_center.x <= bounds.max_x) || (m_center.y >= bounds.min_y && m_center.y <= bounds.max_y);
}

quad_tree_node* quad_tree_node::get_overlapping_child(const aabb& bounds)
{
	if (bounds.max_x < m_center.x)
	{
		if (bounds.max_y < m_center.y)
			return m_children[0].get();
		return m_children[2].get();
	}
	else
	{
		if (bounds.max_y < m_center.y)
			return m_children[1].get();
		return m_children[3].get();
	}
}

void quad_tree_node::query_leaves(const aabb& query, std::vector<actor*>& result)
{
	query_leaves_recursion(query, result);
}

void quad_tree_node::query_leaves_recursion(const aabb& query, std::vector<actor*>& result)
{
	for (int32_t i = 0; i < m_leaves.size(); i++)
	{
		if (m_leaves[i]->get_bounds().overlaps(query))
			result.push_back(m_leaves[i]);
	}
	if (m_children[0] != nullptr)
	{
		for (const auto& child : m_children)
		{
			if (child->m_bounds.overlaps(query))
				child->query_leaves_recursion(query, result);
		}
	}
}
