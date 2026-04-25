#ifndef AABB_H
#define AABB_H

#include <cstdint>

#include "interfaces.h"

namespace emu
{
	// axis aligned bounding box
	// 
	// While it does implement the 'i_collision_shape' interface, 
	// the member functions 'set_pivot' and 'set_rotation' do nothing
	struct aabb : public i_collision_shape
	{
		float min_x;
		float max_x;
		float min_y;
		float max_y;
		float width;
		float height;

		aabb();
		aabb(float min_x, float max_x, float min_y, float max_y);
		shape get_id() const override;
		std::unique_ptr<i_clonable> clone() const override;
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

		void set_from_position_size(vector position, vector size);
		void set_from_shape(i_collision_shape& shape);

		bool overlaps(const aabb& other) const;
		bool contains(vector point) const;
	};
}

#endif
