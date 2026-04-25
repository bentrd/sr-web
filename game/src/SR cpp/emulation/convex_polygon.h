#ifndef CONVEX_POLYGON_H
#define CONVEX_POLYGON_H

#include "interfaces.h"
#include "utility/dyn_array.h"

namespace emu
{
	struct convex_polygon : public i_collision_shape
	{
		vector m_position;
		vector m_pivot;
		float m_rotation;
#ifdef OPTIMIZE_COLLISION
		collidable_type m_collidable_type; // cheating a little here to figure out when a polygon is just a right triangle (slope tile)
#endif
#ifndef OPTIMIZE_COLLISION
		dyn_array<vector> m_local_vertices;
#endif
		dyn_array<vector> m_vertices;
		vector m_center;

		convex_polygon() = default;
		convex_polygon(std::initializer_list<vector> local_vertices);
		convex_polygon(vector position, vector pivot, float rotation, std::initializer_list<vector> local_vertices);

		shape get_id() const override;
		std::unique_ptr<i_clonable> clone() const override;
		void calculate_vertices();
		int32_t get_vertex_count() const override;
		vector get_vertex(int32_t index) const override;
		void get_vertices(std::vector<vector>& vertices) const override;
		vector get_position() const override;
		void set_position(vector position) override;
		vector get_center() const override;
		vector get_pivot() const override;
		void set_pivot(vector pivot) override;
		float get_rotation() const override;
		void set_rotation(float rotation) override;
		void set_position_rotation(vector position, float rotation);
	};
}

#endif
