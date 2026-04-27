// Server-side deterministic replay binary. Compiled to a separate WASM
// module (sr_replay.wasm) that the Bun server loads via WebAssembly.instantiate
// and uses to validate submitted runs against the player's claimed max_speed.
//
// Wire format (matches game/src/SR cpp/playground.h::run_recorder):
//   [varint tickDelta][uint8 bitmask] [varint tickDelta][uint8 bitmask] ...
// First entry has tickDelta=0 and seeds the initial input state.
// Bit layout: 0=left, 1=right, 2=jump, 3=grapple, 4=slide, 5=boost,
//             6=item, 7=swap.
//
// The replay reconstructs the same procedural corridor used by
// playground::load_challenge() and steps the deterministic sim for
// `duration_ticks` ticks, applying the recorded input changes at the
// scheduled ticks.
//
// Returns: peak |velocity| (wu/s) reached during the replay, or 0 on
// any parse error / OOB / log truncation.

#include <cstdint>
#include <cstring>

#include "../src/SR cpp/emulation/level.h"
#include "../src/SR cpp/emulation/state.h"
#include "../src/SR cpp/emulation/player.h"
#include "../src/SR cpp/emulation/actor.h"
#include "../src/SR cpp/emulation/input.h"
#include "../src/SR cpp/emulation/timespan.h"
#include "../src/SR cpp/emulation/game_mode.h"
#include "../src/SR cpp/emulation/rg_detector.h"

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
	float sr_replay_run(
		const unsigned char* log, unsigned int log_len,
		unsigned int duration_ticks)
	{
		if (log == nullptr || log_len == 0) return 0.0f;
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
	// during simulation. Returns 0 on parse error / OOB / log truncation.
	int sr_replay_rg_run(
		const unsigned char* log, unsigned int log_len,
		unsigned int duration_ticks)
	{
		if (log == nullptr || log_len == 0) return 0;
		constexpr unsigned int k_max_ticks = 5u * 3600u * 300u;
		if (duration_ticks > k_max_ticks) return 0;

		emu::level lvl;
		emu::level::generate_corridor(lvl, 100000, 50, 2, 23, 200);

		emu::state st{ lvl };
		st.no_speed_cap = false;

		emu::player* p = st.get_contr<emu::player>(0);
		if (p == nullptr || p->m_actor == nullptr) return 0;

		emu::RgChallengeState rg{};

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
