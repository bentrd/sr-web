#ifndef COLLISION_FILTER_H
#define COLLISION_FILTER_H

#include "common.h"

namespace emu
{
	// collision filter
	//
	// Every actor type with collision has a collision filter, to determine
	// which other actor types it can collide with and which not.
	struct collision_filter
	{
		uint32_t m_category_bits;
		uint32_t m_mask_bits;

		constexpr static uint32_t category_none = 0;
		constexpr static uint32_t category_all = UINT32_MAX;
		constexpr static uint32_t mask_none = 0;
		constexpr static uint32_t mask_all = UINT32_MAX;

		collision_filter();

		constexpr collision_filter(uint32_t category_bits, uint32_t mask_bits) :
			m_category_bits{ category_bits }, m_mask_bits{ mask_bits }
		{

		}

		bool collides_with(collision_filter other) const;
		bool operator==(const collision_filter&) const = default;
	};

	constexpr collision_filter filter_default = collision_filter{ 1u, collision_filter::mask_all };
	constexpr collision_filter filter_none = collision_filter{ 0u, 0u };
	constexpr collision_filter filter_all = collision_filter{ collision_filter::category_all, collision_filter::mask_all };
}

#endif