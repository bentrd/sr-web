#ifndef SR_NETWORK_GHOST_MANAGER_H
#define SR_NETWORK_GHOST_MANAGER_H

#include <mutex>
#include <string>
#include <unordered_map>

#include "ghost_state.h"

namespace net
{
	// Owns all remote-player render snapshots.
	//
	// Updates come from JS (sr_push_ghost / sr_set_ghost_identity / sr_remove_ghost)
	// on whichever thread Emscripten dispatches them — currently the same
	// JS-main thread, but we hold a mutex for safety in case future work
	// adds workers. The render path locks too via with_ghosts().
	struct ghost_manager
	{
		// Upserts a per-frame snapshot. Identity fields (name, color) are
		// preserved if the ghost already exists.
		void push(const std::string& id,
			emu::vector position, emu::vector velocity,
			std::int8_t facing, std::uint8_t anim,
			bool grapple_active,
			emu::vector grapple_origin, emu::vector grapple_attach,
			float grapple_length, bool grapple_taut);

		// Sets the persistent identity for a ghost. Safe to call before
		// the first push; the ghost is created on demand.
		void set_identity(const std::string& id, const std::string& name,
			float r, float g, float b);

		void remove(const std::string& id);
		void clear();

		// Snapshot the current map by value so render can iterate without
		// holding the lock for the whole draw call. Cheap in practice
		// (string ids + a handful of floats per ghost).
		std::unordered_map<std::string, ghost_state> snapshot() const;

		// Lookup by id, returns nullptr if unknown. Holds no lock — use
		// when you've already snapshotted and just need a single peek.
		const ghost_state* try_get(const std::unordered_map<std::string, ghost_state>& snap,
			const std::string& id) const;

	private:
		mutable std::mutex m_mutex;
		std::unordered_map<std::string, ghost_state> m_ghosts;
	};
}

#endif
