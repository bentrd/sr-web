#ifndef LINE_TRACE_H
#define LINE_TRACE_H

#include "common.h"
#include "interfaces.h"

namespace emu
{
	struct line_trace : public i_collision_shape
	{
		vector m_start;
		vector m_end;

		line_trace(vector start, vector end);
		~line_trace() = default;

		void set(vector start, vector end);
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
	};
}

#endif
