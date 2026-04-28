// Server-side deterministic replay binary. Compiled to a separate WASM
// module (sr_replay.wasm) that the Bun server loads via WebAssembly.instantiate
// and uses to validate submitted runs against the player's claimed metric.
//
// Wire format (matches game/src/SR cpp/playground.h::run_recorder):
//   [varint tickDelta][uint8 bitmask] [varint tickDelta][uint8 bitmask] ...
// First entry has tickDelta=0 and seeds the initial input state.
// Bit layout: 0=left, 1=right, 2=jump, 3=grapple, 4=slide, 5=boost,
//             6=item, 7=swap.
//
// The replay reconstructs the same procedural corridor used by
// playground::load_challenge(), restores the player from the submitted
// savestate (so the replay anchors at the exact mid-session pose the
// run was recorded against — not at PlayerStart), then steps the
// deterministic sim for `duration_ticks` ticks, applying the recorded
// input changes at the scheduled ticks.
//
// Returns: peak metric reached during the replay, or 0 on any parse
// error / OOB / log truncation / savestate mismatch.

#include <cstdint>
#include <cstring>

#include "../src/SR cpp/emulation/level.h"
#include "../src/SR cpp/emulation/state.h"
#include "../src/SR cpp/emulation/player.h"
#include "../src/SR cpp/emulation/grapple.h"
#include "../src/SR cpp/emulation/actor.h"
#include "../src/SR cpp/emulation/input.h"
#include "../src/SR cpp/emulation/timespan.h"
#include "../src/SR cpp/emulation/game_mode.h"
#include "../src/SR cpp/emulation/rg_detector.h"

namespace
{
	// Mirror of playground.h's k_savestate_magic / k_savestate_version /
	// savestate_header. Kept inline here so the replay binary doesn't
	// pull in playground.h (which transitively pulls in OpenGL headers).
	constexpr std::uint32_t k_savestate_magic = 0x56415350u; // 'PSAV'
	constexpr std::uint32_t k_savestate_version = 1;

	struct savestate_header
	{
		std::uint32_t magic;
		std::uint32_t version;
		std::uint32_t player_d_size;
		std::uint32_t actor_d_size;
		std::uint32_t grapple_d_size;
		std::uint32_t grapple_actor_d_size;
		std::uint32_t rg_state_size;
		std::uint32_t flags;
		std::int64_t state_time_ticks;
		std::uint64_t reserved;
	};
	static_assert(sizeof(savestate_header) == 48, "header layout drift");

	// Apply a client-captured savestate to the freshly-spawned `st`/`p`/
	// `rg`. Returns true on full apply. False on any size / magic /
	// version mismatch — the caller treats that as a verdict=fail.
	bool apply_savestate(emu::state& st, emu::player& p, emu::RgChallengeState& rg,
		const unsigned char* in, unsigned int in_size)
	{
		if (in == nullptr || in_size < sizeof(savestate_header)) return false;

		savestate_header h{};
		std::memcpy(&h, in, sizeof(h));
		if (h.magic != k_savestate_magic) return false;
		if (h.version != k_savestate_version) return false;
		if (h.player_d_size != sizeof(p.d)) return false;
		if (p.m_actor == nullptr) return false;
		if (h.actor_d_size != sizeof(p.m_actor->d)) return false;
		if (h.rg_state_size != sizeof(rg)) return false;

		const bool has_grapple = (h.flags & 1u) != 0u;
		if (has_grapple)
		{
			if (p.m_grapple == nullptr || p.m_grapple->m_actor == nullptr) return false;
			if (h.grapple_d_size != sizeof(p.m_grapple->d)) return false;
			if (h.grapple_actor_d_size != sizeof(p.m_grapple->m_actor->d)) return false;
		}
		else if (h.grapple_d_size != 0 || h.grapple_actor_d_size != 0)
		{
			return false;
		}

		const std::size_t need = sizeof(savestate_header) + h.player_d_size + h.actor_d_size
			+ h.grapple_d_size + h.grapple_actor_d_size + h.rg_state_size;
		if (in_size < need) return false;

		std::size_t off = sizeof(savestate_header);
		std::memcpy(&p.d, in + off, h.player_d_size); off += h.player_d_size;
		std::memcpy(&p.m_actor->d, in + off, h.actor_d_size); off += h.actor_d_size;
		if (has_grapple)
		{
			std::memcpy(&p.m_grapple->d, in + off, h.grapple_d_size); off += h.grapple_d_size;
			std::memcpy(&p.m_grapple->m_actor->d, in + off, h.grapple_actor_d_size); off += h.grapple_actor_d_size;
		}
		std::memcpy(&rg, in + off, h.rg_state_size); off += h.rg_state_size;

		p.m_actor->d.position_changed = true;
		p.update_hitboxes();
		st.m_time = emu::timespan{ static_cast<std::uint64_t>(h.state_time_ticks) };
		return true;
	}
}

extern "C"
{
	// Decoded LEB128 varint into *out, returning the new log offset, or
	// log_len + 1 on overflow / truncation (caller treats anything >
	// log_len as failure).
	static unsigned int read_varint(const unsigned char* log, unsigned int log_len,
		unsigned int pos, std::uint64_t* out)
	{
		std::uint64_t v = 0;
		unsigned int shift = 0;
		while (pos < log_len)
		{
			const std::uint8_t b = log[pos++];
			v |= static_cast<std::uint64_t>(b & 0x7f) << shift;
			if ((b & 0x80) == 0)
			{
				*out = v;
				return pos;
			}
			shift += 7;
			if (shift >= 64) return log_len + 1; // malformed
		}
		return log_len + 1; // truncated
	}

	// Replay the recorded inputs and return peak |velocity| (wu/s) seen
	// during simulation. duration_ticks is the number of sim steps to run
	// (each step is 33333 .NET TimeSpan ticks = 1/300s).
	//
	// `savestate` carries the player's full physics pose at the moment
	// the run was armed (see playground::capture_savestate). It is
	// applied to the freshly-spawned player before any sim steps run,
	// so the replay anchors at the exact mid-session state the run was
	// recorded against — not at PlayerStart.
	float sr_replay_run(
		const unsigned char* log, unsigned int log_len,
		unsigned int duration_ticks,
		const unsigned char* savestate, unsigned int savestate_len)
	{
		if (log == nullptr || log_len == 0) return 0.0f;
		if (savestate == nullptr || savestate_len == 0) return 0.0f;
		// Hard upper bound on duration to avoid pathological input that
		// claims a multi-day replay. 5h at 300 Hz matches the server-side
		// validator's RUN_DURATION_TICKS_MAX.
		constexpr unsigned int k_max_ticks = 5u * 3600u * 300u;
		if (duration_ticks > k_max_ticks) return 0.0f;

		// Match playground::load_challenge(): 100K-tile-wide corridor,
		// ceiling at row 2, floor at row 23, spawn at column 200.
		emu::level lvl;
		emu::level::generate_corridor(lvl, 100000, 50, 2, 23, 200);

		emu::state st{ lvl };
		st.no_speed_cap = true;

		emu::player* p = st.get_contr<emu::player>(0);
		if (p == nullptr || p->m_actor == nullptr) return 0.0f;

		// Apply the run's starting pose. Any size / magic / version
		// mismatch is a verdict=fail.
		emu::RgChallengeState rg_unused{};
		if (!apply_savestate(st, *p, rg_unused, savestate, savestate_len))
			return 0.0f;

		// Decode the seed event (must be delta=0 + bitmask). Anything else
		// is malformed — bail rather than silently substitute zero inputs.
		std::uint64_t event_tick = 0;
		unsigned int log_pos = 0;
		std::uint8_t event_bitmask = 0;
		bool have_event = false;

		std::uint64_t delta = 0;
		log_pos = read_varint(log, log_len, log_pos, &delta);
		if (log_pos > log_len || log_pos >= log_len) return 0.0f;
		event_tick = delta; // must be 0 by protocol, but we don't enforce
		event_bitmask = log[log_pos++];
		have_event = true;

		std::uint8_t current_bitmask = 0;
		float max_speed = 0.0f;

		for (unsigned int t = 0; t < duration_ticks; t++)
		{
			// Drain all events scheduled for this tick. Multiple events
			// at the same tick is impossible in well-formed logs, but we
			// loop defensively rather than asserting.
			while (have_event && event_tick == static_cast<std::uint64_t>(t))
			{
				current_bitmask = event_bitmask;
				if (log_pos >= log_len)
				{
					have_event = false;
					break;
				}
				std::uint64_t d = 0;
				log_pos = read_varint(log, log_len, log_pos, &d);
				if (log_pos > log_len || log_pos >= log_len)
				{
					have_event = false;
					break;
				}
				event_tick += d;
				event_bitmask = log[log_pos++];
			}

			// Apply current bitmask to player 0's input slots. Other
			// players don't exist in challenge replay.
			for (std::size_t i = 0; i < emu::input_count; i++)
				st.m_inputs[0][i] = ((current_bitmask >> i) & 1u) != 0;

			// Step the sim (33333 .NET TimeSpan ticks = 1/300s).
			st.update(emu::timespan{ 33333ull });

			// Track peak speed.
			const float speed = p->m_actor->d.velocity.length();
			if (speed > max_speed) max_speed = speed;
		}

		return max_speed;
	}

	// RG-challenge replay. Same procedural corridor + sim, but with the
	// speed cap left ON (no_speed_cap = false) and the RG detector running
	// after each step. Returns the peak streak (session_best) reached
	// during simulation. Returns 0 on parse error / OOB / log truncation
	// / savestate mismatch.
	//
	// The savestate also restores the player's `RgChallengeState` so the
	// streak counter resumes from exactly where it stood at arm time
	// (mid-session arms produce non-zero starting streaks).
	int sr_replay_rg_run(
		const unsigned char* log, unsigned int log_len,
		unsigned int duration_ticks,
		const unsigned char* savestate, unsigned int savestate_len)
	{
		if (log == nullptr || log_len == 0) return 0;
		if (savestate == nullptr || savestate_len == 0) return 0;
		constexpr unsigned int k_max_ticks = 5u * 3600u * 300u;
		if (duration_ticks > k_max_ticks) return 0;

		emu::level lvl;
		emu::level::generate_corridor(lvl, 100000, 50, 2, 23, 200);

		emu::state st{ lvl };
		st.no_speed_cap = false;

		emu::player* p = st.get_contr<emu::player>(0);
		if (p == nullptr || p->m_actor == nullptr) return 0;

		emu::RgChallengeState rg{};
		if (!apply_savestate(st, *p, rg, savestate, savestate_len))
			return 0;

		std::uint64_t event_tick = 0;
		unsigned int log_pos = 0;
		std::uint8_t event_bitmask = 0;
		bool have_event = false;

		std::uint64_t delta = 0;
		log_pos = read_varint(log, log_len, log_pos, &delta);
		if (log_pos > log_len || log_pos >= log_len) return 0;
		event_tick = delta;
		event_bitmask = log[log_pos++];
		have_event = true;

		std::uint8_t current_bitmask = 0;

		for (unsigned int t = 0; t < duration_ticks; t++)
		{
			while (have_event && event_tick == static_cast<std::uint64_t>(t))
			{
				current_bitmask = event_bitmask;
				if (log_pos >= log_len)
				{
					have_event = false;
					break;
				}
				std::uint64_t d = 0;
				log_pos = read_varint(log, log_len, log_pos, &d);
				if (log_pos > log_len || log_pos >= log_len)
				{
					have_event = false;
					break;
				}
				event_tick += d;
				event_bitmask = log[log_pos++];
			}

			for (std::size_t i = 0; i < emu::input_count; i++)
				st.m_inputs[0][i] = ((current_bitmask >> i) & 1u) != 0;

			st.update(emu::timespan{ 33333ull });

			emu::update_rg_state(rg, *p, st.m_time);
		}

		return rg.session_best;
	}
}
