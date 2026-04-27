// Server-side deterministic replay validator. Loads the sr_replay.wasm
// module (built from emulation/* + game/platform/replay_main.cpp) and
// exposes a single replayRun() entry point that the submit_run handler
// uses to verify a player's claimed max_speed.
//
// The validator runs the same C++ sim used in the browser, so as long as
// both the client and server load the same WASM bytecode the replay is
// bit-exact (modulo IEEE-754 rounding which is itself deterministic
// inside WebAssembly).

import { createRequire } from "node:module";
import { join } from "node:path";

interface ReplayModule {
	_sr_replay_run: (logPtr: number, logLen: number, durationTicks: number) => number;
	_sr_replay_rg_run: (logPtr: number, logLen: number, durationTicks: number) => number;
	_malloc: (size: number) => number;
	_free: (ptr: number) => void;
	HEAPU8: Uint8Array;
}

let modulePromise: Promise<ReplayModule> | null = null;

async function loadModule(): Promise<ReplayModule> {
	if (modulePromise) return modulePromise;

	// The Emscripten loader is emitted as CommonJS. require() it relative
	// to this file so it works regardless of cwd at boot. The path is
	// game/CMakeLists.txt → apps/server/wasm/sr_replay.{js,wasm}.
	const require = createRequire(import.meta.url);
	const factoryPath = join(import.meta.dir, "..", "wasm", "sr_replay.js");
	type Factory = (opts?: object) => Promise<ReplayModule>;
	const factory = require(factoryPath) as Factory;

	modulePromise = factory({}) as Promise<ReplayModule>;
	return modulePromise;
}

export interface ReplayResult {
	ok: boolean;
	maxSpeed: number;
	error?: string;
}

// Replay the recorded inputs and return the peak |velocity| reached
// during simulation. Errors (OOM, malformed log, runtime crash) are
// surfaced as { ok: false } so callers can mark the run unverified
// instead of mis-classifying it as a cheat.
export async function replayRun(
	inputs: Uint8Array,
	durationTicks: number,
): Promise<ReplayResult> {
	if (inputs.length === 0) return { ok: false, maxSpeed: 0, error: "empty inputs" };
	if (!Number.isInteger(durationTicks) || durationTicks <= 0) {
		return { ok: false, maxSpeed: 0, error: "invalid durationTicks" };
	}

	let mod: ReplayModule;
	try {
		mod = await loadModule();
	} catch (e) {
		return {
			ok: false,
			maxSpeed: 0,
			error: e instanceof Error ? `module load: ${e.message}` : "module load failed",
		};
	}

	let ptr = 0;
	try {
		ptr = mod._malloc(inputs.length);
		if (!ptr) return { ok: false, maxSpeed: 0, error: "malloc failed" };
		mod.HEAPU8.set(inputs, ptr);
		const speed = mod._sr_replay_run(ptr, inputs.length, durationTicks);
		if (!Number.isFinite(speed)) {
			return { ok: false, maxSpeed: 0, error: "non-finite max_speed" };
		}
		return { ok: true, maxSpeed: speed };
	} catch (e) {
		return {
			ok: false,
			maxSpeed: 0,
			error: e instanceof Error ? e.message : "replay crashed",
		};
	} finally {
		if (ptr) mod._free(ptr);
	}
}

// Tolerance for matching replayed max_speed against the client-claimed
// value. Same WASM bytecode → bit-exact in theory; allow a tiny floor of
// 0.5 wu/s + 0.5% relative to absorb engine-level edge cases (Bun JIT
// vs. V8 vs. browser WASM, different SIMD codegen on host CPUs).
export function speedMatches(claimed: number, actual: number): boolean {
	const absoluteSlack = 0.5;
	const relativeSlack = 0.005;
	const tolerance = Math.max(absoluteSlack, claimed * relativeSlack);
	return Math.abs(claimed - actual) <= tolerance;
}

export interface RgReplayResult {
	ok: boolean;
	maxStreak: number;
	error?: string;
}

// RG-mode counterpart to replayRun. Replays the input log and returns
// the peak streak (session_best) reached during simulation. Same
// determinism story as speedRun: the same WASM bytecode runs the same
// RG detector, so a non-cheating client should produce a bit-exact
// integer match.
export async function replayRgRun(
	inputs: Uint8Array,
	durationTicks: number,
): Promise<RgReplayResult> {
	if (inputs.length === 0) return { ok: false, maxStreak: 0, error: "empty inputs" };
	if (!Number.isInteger(durationTicks) || durationTicks <= 0) {
		return { ok: false, maxStreak: 0, error: "invalid durationTicks" };
	}

	let mod: ReplayModule;
	try {
		mod = await loadModule();
	} catch (e) {
		return {
			ok: false,
			maxStreak: 0,
			error: e instanceof Error ? `module load: ${e.message}` : "module load failed",
		};
	}

	let ptr = 0;
	try {
		ptr = mod._malloc(inputs.length);
		if (!ptr) return { ok: false, maxStreak: 0, error: "malloc failed" };
		mod.HEAPU8.set(inputs, ptr);
		const streak = mod._sr_replay_rg_run(ptr, inputs.length, durationTicks);
		if (!Number.isInteger(streak) || streak < 0) {
			return { ok: false, maxStreak: 0, error: "invalid max_streak" };
		}
		return { ok: true, maxStreak: streak };
	} catch (e) {
		return {
			ok: false,
			maxStreak: 0,
			error: e instanceof Error ? e.message : "replay crashed",
		};
	} finally {
		if (ptr) mod._free(ptr);
	}
}

// RG max_streak is an integer, so we expect bit-exact equality. Allow a
// ±1 slack only to absorb a single-frame off-by-one between client and
// server (e.g. a streak that increments on the same tick the snapshot
// is taken).
export function streakMatches(claimed: number, actual: number): boolean {
	return Math.abs(claimed - actual) <= 1;
}
