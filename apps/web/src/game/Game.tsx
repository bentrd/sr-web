// React host for the WASM game.
//
// Lifecycle:
// 1. Mount canvas, load sr.js, await createSrModule({ canvas })
// 2. Resolve cwrap'd C ABI handles
// 3. sr_set_local_identity + sr_load_map (for the room's chosen map)
// 4. Per-tick (rAF): refresh name overlay positions; interpolate ghost
//    snapshots and push the lerped state via sr_push_ghost
// 5. Per 16ms (setInterval): sr_get_local_snapshot → ws.send snapshot
// 6. WS snapshot arrives → sr_set_ghost_identity (first time per peer) +
//    push into per-peer interpolation buffer
// 7. WS player_left → sr_remove_ghost + drop buffer
// 8. Unmount: stop intervals + rAF, leave the WASM alive (factory caches)

import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { SNAPSHOT_BYTES } from "@sr-web/protocol";
import type { ServerMsg } from "@sr-web/protocol";
import { useApp } from "../state/AppState";
import { rgbToCss } from "../lobby/color";
import { loadSrModule, type SrModule } from "../wasm/loadModule";
import { base64ToBytes, bytesToBase64, decodeSnapshot, type DecodedSnapshot } from "./snapshotCodec";
import { GAME_ACTIONS, eventToBinding } from "../state/bindings";
import { pollGamepads } from "./gamepad";
import { speedColor } from "../state/visuals";
import { loadDefaultTrail, loadTrailFromBytes, bindTrailAbi } from "./trail/loadTrail";
import { base64ToBytes as srtBase64ToBytes } from "./trail/parseSrt";
import { QuickChatMenu } from "./QuickChatMenu";
import type { QuickChatSlot } from "../state/quickChat";

// 60 Hz network send rate. Sim runs at ~300 Hz inside WASM, render at
// monitor refresh — the three are deliberately decoupled (see AGENTS.md).
const SEND_INTERVAL_MS = 16;

	// Render ghosts INTERP_DELAY_MS in the past so we always have one
	// snapshot ahead to lerp toward. Smaller = lower perceived latency, but
	// any network jitter > delay causes a stutter. With 60Hz sends, ~60ms
	// gives us 3-4 samples of cushion.
	const INTERP_DELAY_MS = 60;
	// Cap how many samples we retain per peer. We only ever need the two
	// bracketing the render time; an extra slot or two protects against
	// out-of-order arrivals.
	const GHOST_BUFFER_MAX = 6;

	// Leaderboard fetch URL (derived from the WS URL).
	const LEADERBOARD_URL = (import.meta.env.VITE_WS_URL ?? "ws://localhost:4000/ws")
		.replace(/^ws/, "http")
		.replace(/\/ws$/, "/leaderboard");

interface GhostSample {
	recvTime: number; // performance.now() at WS receipt
	snap: DecodedSnapshot;
}

function lerp(a: number, b: number, t: number): number {
	return a + (b - a) * t;
}

function lerpSample(s0: DecodedSnapshot, s1: DecodedSnapshot, t: number): DecodedSnapshot {
	// Continuous fields lerp; discrete fields snap at the midpoint so we
	// don't draw fractional facing/anim states. Grapple endpoints only
	// lerp when both samples agree the grapple is active — otherwise we'd
	// interpolate from (0,0) to a real point and draw a rope from origin.
	const bothActive = s0.grappleActive && s1.grappleActive;
	const useS1 = t >= 0.5;
	return {
		posX: lerp(s0.posX, s1.posX, t),
		posY: lerp(s0.posY, s1.posY, t),
		velX: lerp(s0.velX, s1.velX, t),
		velY: lerp(s0.velY, s1.velY, t),
		facing: useS1 ? s1.facing : s0.facing,
		anim: useS1 ? s1.anim : s0.anim,
		grappleActive: useS1 ? s1.grappleActive : s0.grappleActive,
		grappleTaut: useS1 ? s1.grappleTaut : s0.grappleTaut,
		grappleOriginX: bothActive ? lerp(s0.grappleOriginX, s1.grappleOriginX, t) : (useS1 ? s1.grappleOriginX : s0.grappleOriginX),
		grappleOriginY: bothActive ? lerp(s0.grappleOriginY, s1.grappleOriginY, t) : (useS1 ? s1.grappleOriginY : s0.grappleOriginY),
		grappleAttachX: bothActive ? lerp(s0.grappleAttachX, s1.grappleAttachX, t) : (useS1 ? s1.grappleAttachX : s0.grappleAttachX),
		grappleAttachY: bothActive ? lerp(s0.grappleAttachY, s1.grappleAttachY, t) : (useS1 ? s1.grappleAttachY : s0.grappleAttachY),
		grappleLength: bothActive ? lerp(s0.grappleLength, s1.grappleLength, t) : (useS1 ? s1.grappleLength : s0.grappleLength),
		sizeX: useS1 ? s1.sizeX : s0.sizeX,
		sizeY: useS1 ? s1.sizeY : s0.sizeY,
	};
}

interface CAbi {
	setLocalIdentity: (name: string, r: number, g: number, b: number) => void;
	loadMap: (path: string) => void;
	loadChallenge: () => void;
	getMaxSpeed: () => number;
	resetChallenge: () => void;
	loadRgChallenge: () => void;
	getRgConsecutive: () => number;
	getRgBest: () => number;
	resetRgChallenge: () => void;
	loadTimeChallenge: () => void;
	resetTimeChallenge: () => void;
	getTimeRunElapsedTicks: () => number;
	pushGhost: (
		id: string,
		posX: number, posY: number,
		velX: number, velY: number,
		facing: number, anim: number,
		grappleActive: number,
		gxOrigin: number, gyOrigin: number,
		gxAttach: number, gyAttach: number,
		gLength: number, gTaut: number,
		sizeX: number, sizeY: number,
	) => void;
	setGhostIdentity: (id: string, name: string, r: number, g: number, b: number) => void;
	removeGhost: (id: string) => void;
	getLocalSnapshot: () => Uint8Array | null;
	getPlayerScreenPos: (id: string) => { x: number; y: number } | null;
	setBinding: (action: number, glfwKey: number) => void;
	pushControllerInput: (action: number, pressed: number) => void;
	teleportLocal: (x: number, y: number) => void;
	resetLocal: () => void;
	setTargetFps: (fps: number) => void;
	setVisualBg: (r: number, g: number, b: number) => void;
	setVisualWalls: (r: number, g: number, b: number) => void;
	setVisualGrappleStripe: (r: number, g: number, b: number) => void;
	setVisualWallclimbStripe: (r: number, g: number, b: number) => void;
	setVisualGrappleCord: (r: number, g: number, b: number) => void;
	setVisualGrappleHead: (r: number, g: number, b: number) => void;
	setVisualGrappleHeadSize: (size: number) => void;
	setVisualBoostSection: (r: number, g: number, b: number, a: number) => void;
	setVisualBoostPickup: (r: number, g: number, b: number, a: number) => void;
	setVisualShowBoostBar: (show: boolean) => void;
	setVisualRgGrid: (show: boolean) => void;
	saveState: () => void;
	loadState: () => boolean;
	// Anti-cheat run recorder. consumeFinishedRun returns null when no run
	// is pending, otherwise the bytes + savestate + scoring metadata for
	// the run that just ended (and re-arms the recorder against the
	// current player state). `savestate` carries the player's full physics
	// pose at arm time so the server can replay from that exact mid-session
	// pose, not from PlayerStart.
	runSimVersion: () => number;
	consumeFinishedRun: () => {
		inputs: Uint8Array;
		savestate: Uint8Array;
		maxSpeed: number;
		maxStreak: number;
		durationTicks: number;
	} | null;
	// Browser-side replay playback. mode: 0=speed challenge, 1=RG challenge.
	// Loads the level fresh, applies the savestate so playback starts from
	// the exact mid-session pose the run was recorded against, then drives
	// the sim from the replay buffer until exhausted.
	startReplay: (
		inputs: Uint8Array,
		durationTicks: number,
		mode: number,
		savestate: Uint8Array,
	) => boolean;
	stopReplay: () => void;
	isReplayActive: () => boolean;
	replayProgressPermille: () => number;
}

function bindCAbi(mod: SrModule): CAbi {
	const f_set_id = mod.cwrap("sr_set_local_identity", null, ["string", "number", "number", "number"]);
	const f_load = mod.cwrap("sr_load_map", null, ["string"]);
	const f_load_ch = mod.cwrap("sr_load_challenge", null, []);
	const f_max_sp = mod.cwrap("sr_get_max_speed", "number", []);
	const f_reset_ch = mod.cwrap("sr_reset_challenge", null, []);
	const f_load_rg_ch = mod.cwrap("sr_load_rg_challenge", null, []);
	const f_rg_consecutive = mod.cwrap("sr_get_rg_consecutive", "number", []);
	const f_rg_best = mod.cwrap("sr_get_rg_best", "number", []);
	const f_reset_rg_ch = mod.cwrap("sr_reset_rg_challenge", null, []);
	const f_load_time_ch = mod.cwrap("sr_load_time_challenge", null, []);
	const f_reset_time_ch = mod.cwrap("sr_reset_time_challenge", null, []);
	const f_time_elapsed = mod.cwrap("sr_time_run_elapsed_ticks", "number", []);
	const f_push = mod.cwrap("sr_push_ghost", null, [
		"string",
		"number", "number",
		"number", "number",
		"number", "number",
		"number",
		"number", "number",
		"number", "number",
		"number", "number",
		"number", "number",
	]);
	const f_set_ghost_id = mod.cwrap("sr_set_ghost_identity", null, ["string", "string", "number", "number", "number"]);
	const f_remove = mod.cwrap("sr_remove_ghost", null, ["string"]);
	const f_get_snap = mod.cwrap("sr_get_local_snapshot", "number", ["number", "number"]);
	const f_get_pos = mod.cwrap("sr_get_player_screen_pos", "number", ["string", "number", "number"]);
	const f_set_binding = mod.cwrap("sr_set_binding", null, ["number", "number"]);
	const f_controller = mod.cwrap("sr_push_controller_input", null, ["number", "number"]);
	const f_teleport = mod.cwrap("sr_teleport_local", null, ["number", "number"]);
	const f_reset = mod.cwrap("sr_reset_local", null, []);
	const f_set_fps = mod.cwrap("sr_set_target_fps", null, ["number"]);
	const f_v_bg = mod.cwrap("sr_set_visual_bg", null, ["number", "number", "number"]);
	const f_v_walls = mod.cwrap("sr_set_visual_walls", null, ["number", "number", "number"]);
	const f_v_grapple_stripe = mod.cwrap("sr_set_visual_grapple_stripe", null, ["number", "number", "number"]);
	const f_v_wallclimb_stripe = mod.cwrap("sr_set_visual_wallclimb_stripe", null, ["number", "number", "number"]);
	const f_v_grapple_cord = mod.cwrap("sr_set_visual_grapple_cord", null, ["number", "number", "number"]);
	const f_v_grapple_head = mod.cwrap("sr_set_visual_grapple_head", null, ["number", "number", "number"]);
	const f_v_grapple_head_size = mod.cwrap("sr_set_visual_grapple_head_size", null, ["number"]);
	const f_v_boost_section = mod.cwrap("sr_set_visual_boost_section", null, ["number", "number", "number", "number"]);
	const f_v_boost_pickup = mod.cwrap("sr_set_visual_boost_pickup", null, ["number", "number", "number", "number"]);
	const f_v_show_boost_bar = mod.cwrap("sr_set_visual_show_boost_bar", null, ["number"]);
	const f_v_rg_grid = mod.cwrap("sr_set_visual_rg_grid", null, ["number"]);
	const f_save_state = mod.cwrap("sr_save_state", null, []);
	const f_load_state = mod.cwrap("sr_load_state", "number", []);
	const f_run_sim_ver = mod.cwrap("sr_run_sim_version", "number", []);
	const f_run_finished = mod.cwrap("sr_run_is_finished", "number", []);
	const f_run_log_size = mod.cwrap("sr_run_finished_log_size", "number", []);
	const f_run_savestate_size = mod.cwrap("sr_run_finished_savestate_size", "number", []);
	const f_run_consume = mod.cwrap("sr_run_consume_finished", "number", [
		"number", "number", "number", "number", "number", "number", "number",
	]);
	const f_replay_start = mod.cwrap("sr_replay_start", "number", [
		"number", "number", "number", "number", "number", "number",
	]);
	const f_replay_stop = mod.cwrap("sr_replay_stop", null, []);
	const f_replay_active = mod.cwrap("sr_replay_is_active", "number", []);
	const f_replay_progress = mod.cwrap("sr_replay_progress_permille", "number", []);

	// Persistent scratch buffers in WASM heap. Allocated once; freed on
	// page unload. malloc/free are exported but we never hit them more
	// than this so the cost is amortised.
	const snapPtr = mod._malloc(SNAPSHOT_BYTES);
	const xPtr = mod._malloc(4);
	const yPtr = mod._malloc(4);
	// Out-params for sr_run_consume_finished + scratch buffers big enough
	// for the hardest caps (256 KB raw log, 4 KB raw savestate). Kept
	// resident — runs end at most once per floor touch so the cost of
	// re-allocating per call is silly.
	const RUN_LOG_CAP = 256 * 1024;
	const RUN_SAVESTATE_CAP = 4 * 1024;
	const runLogPtr = mod._malloc(RUN_LOG_CAP);
	const runSavestatePtr = mod._malloc(RUN_SAVESTATE_CAP);
	const runMaxSpeedPtr = mod._malloc(4);
	const runMaxStreakPtr = mod._malloc(4);
	const runDurationPtr = mod._malloc(4);

	return {
		setLocalIdentity: (name, r, g, b) => {
			f_set_id(name, r, g, b);
		},
		loadMap: (path) => {
			f_load(path);
		},
		loadChallenge: () => {
			f_load_ch();
		},
		getMaxSpeed: () => {
			return f_max_sp() as number;
		},
		resetChallenge: () => {
			f_reset_ch();
		},
		loadRgChallenge: () => { f_load_rg_ch(); },
		getRgConsecutive: () => f_rg_consecutive() as number,
		getRgBest: () => f_rg_best() as number,
		resetRgChallenge: () => { f_reset_rg_ch(); },
		loadTimeChallenge: () => { f_load_time_ch(); },
		resetTimeChallenge: () => { f_reset_time_ch(); },
		getTimeRunElapsedTicks: () => f_time_elapsed() as number,
		pushGhost: (id, posX, posY, velX, velY, facing, anim, grappleActive, gxOrigin, gyOrigin, gxAttach, gyAttach, gLength, gTaut, sizeX, sizeY) => {
			f_push(
				id,
				posX, posY,
				velX, velY,
				facing, anim,
				grappleActive,
				gxOrigin, gyOrigin,
				gxAttach, gyAttach,
				gLength, gTaut,
				sizeX, sizeY,
			);
		},
		setGhostIdentity: (id, name, r, g, b) => {
			f_set_ghost_id(id, name, r, g, b);
		},
		removeGhost: (id) => {
			f_remove(id);
		},
		getLocalSnapshot: () => {
			const written = f_get_snap(snapPtr, SNAPSHOT_BYTES) as number;
			if (written < SNAPSHOT_BYTES) return null;
			// Copy out — HEAPU8 view will be invalidated if WASM grows memory.
			return mod.HEAPU8.slice(snapPtr, snapPtr + SNAPSHOT_BYTES);
		},
		getPlayerScreenPos: (id) => {
			const ok = f_get_pos(id, xPtr, yPtr) as number;
			if (!ok) return null;
			// Recompute view from current HEAPF32 (might have grown after
			// loadMap or other allocations).
			const x = mod.HEAPF32[xPtr >> 2] ?? 0;
			const y = mod.HEAPF32[yPtr >> 2] ?? 0;
			return { x, y };
		},
		setBinding: (action, glfwKey) => {
			f_set_binding(action, glfwKey);
		},
		pushControllerInput: (action, pressed) => {
			f_controller(action, pressed);
		},
		teleportLocal: (x, y) => {
			f_teleport(x, y);
		},
		resetLocal: () => {
			f_reset();
		},
		setTargetFps: (fps) => {
			f_set_fps(fps);
		},
		setVisualBg: (r, g, b) => { f_v_bg(r, g, b); },
		setVisualWalls: (r, g, b) => { f_v_walls(r, g, b); },
		setVisualGrappleStripe: (r, g, b) => { f_v_grapple_stripe(r, g, b); },
		setVisualWallclimbStripe: (r, g, b) => { f_v_wallclimb_stripe(r, g, b); },
		setVisualGrappleCord: (r, g, b) => { f_v_grapple_cord(r, g, b); },
		setVisualGrappleHead: (r, g, b) => { f_v_grapple_head(r, g, b); },
		setVisualGrappleHeadSize: (size) => { f_v_grapple_head_size(size); },
		setVisualBoostSection: (r, g, b, a) => { f_v_boost_section(r, g, b, a); },
		setVisualBoostPickup: (r, g, b, a) => { f_v_boost_pickup(r, g, b, a); },
		setVisualShowBoostBar: (show) => { f_v_show_boost_bar(show ? 1 : 0); },
		setVisualRgGrid: (show) => { f_v_rg_grid(show ? 1 : 0); },
		saveState: () => { f_save_state(); },
		loadState: () => (f_load_state() as number) !== 0,
		runSimVersion: () => f_run_sim_ver() as number,
		consumeFinishedRun: () => {
			if ((f_run_finished() as number) === 0) return null;
			const logNeed = f_run_log_size() as number;
			if (logNeed <= 0 || logNeed > RUN_LOG_CAP) return null;
			const ssNeed = f_run_savestate_size() as number;
			if (ssNeed <= 0 || ssNeed > RUN_SAVESTATE_CAP) return null;
			const written = f_run_consume(
				runLogPtr, RUN_LOG_CAP,
				runSavestatePtr, RUN_SAVESTATE_CAP,
				runMaxSpeedPtr, runMaxStreakPtr, runDurationPtr,
			) as number;
			if (written === 0) return null;
			// HEAPU8 view may move after WASM allocations — re-read each call.
			const inputs = mod.HEAPU8.slice(runLogPtr, runLogPtr + written);
			const savestate = mod.HEAPU8.slice(runSavestatePtr, runSavestatePtr + ssNeed);
			const maxSpeed = mod.HEAPF32[runMaxSpeedPtr >> 2] ?? 0;
			const maxStreak = new Int32Array(mod.HEAPU8.buffer, runMaxStreakPtr, 1)[0] ?? 0;
			const dur32 = new Uint32Array(mod.HEAPU8.buffer, runDurationPtr, 1)[0] ?? 0;
			return { inputs, savestate, maxSpeed, maxStreak, durationTicks: dur32 };
		},
		startReplay: (inputs, durationTicks, mode, savestate) => {
			if (inputs.length === 0 || inputs.length > RUN_LOG_CAP) return false;
			if (savestate.length === 0 || savestate.length > RUN_SAVESTATE_CAP) return false;
			// Reuse the run-log scratch buffer — recorder gets cleared by
			// start_replay on the C++ side, so there's no overlap.
			mod.HEAPU8.set(inputs, runLogPtr);
			mod.HEAPU8.set(savestate, runSavestatePtr);
			const ok = f_replay_start(
				runLogPtr, inputs.length, durationTicks, mode,
				runSavestatePtr, savestate.length,
			) as number;
			return ok !== 0;
		},
		stopReplay: () => { f_replay_stop(); },
		isReplayActive: () => (f_replay_active() as number) !== 0,
		replayProgressPermille: () => f_replay_progress() as number,
	};
}

interface HoveredLabel {
	id: string;
	name: string;
	color: string;
	x: number;
	y: number;
}

// Hover hit-radius in canvas pixels — roughly the player rectangle's
// half-diagonal so the label appears as soon as the cursor enters the body.
const HOVER_RADIUS_PX = 30;
const HOVER_RADIUS_SQ = HOVER_RADIUS_PX * HOVER_RADIUS_PX;

// 300 ticks/sec; format as xx.xxx seconds.
function formatTicksAsSeconds(ticks: number): string {
	return (ticks / 300).toFixed(3);
}

export function Game(): JSX.Element {
	const { ws, identity, bindings, targetFps, visuals, room, playerId, quickChat, sendChat } = useApp();
	const canvasRef = useRef<HTMLCanvasElement | null>(null);
	const containerRef = useRef<HTMLDivElement | null>(null);
	const abiRef = useRef<CAbi | null>(null);
	const trailAbiRef = useRef<ReturnType<typeof bindTrailAbi> | null>(null);
	const loadedPeerTrailsRef = useRef<Set<string>>(new Set());
	const [status, setStatus] = useState<"loading" | "ready" | "error">("loading");
	const [error, setError] = useState<string | null>(null);
	const [hovered, setHovered] = useState<HoveredLabel | null>(null);
	const [localSpeed, setLocalSpeed] = useState<number | null>(null);
	
	const cursorRef = useRef<{ x: number; y: number } | null>(null);
	const [quickChatMenu, setQuickChatMenu] = useState<number | null>(null);

	// Speed Challenge state
	const isChallenge = room?.mode === "grapple_challenge";
	const isRgChallenge = room?.mode === "rg_challenge";
	const isTimeChallenge = room?.mode === "time_challenge";
	const [sessionMax, setSessionMax] = useState<number>(0);
	const [rgConsecutive, setRgConsecutive] = useState<number>(0);
	const [rgBest, setRgBest] = useState<number>(0);
	const [timeElapsed, setTimeElapsed] = useState<number>(0);
	const [scoreAck, setScoreAck] = useState<{ rank: number; dailyBest: number } | null>(null);
	const [timeScoreAck, setTimeScoreAck] = useState<{ rank: number; bestTicks: number } | null>(null);
	const [leaderboardEntries, setLeaderboardEntries] = useState<Array<{ rank: number; name: string; value: number; display: string; runId: number | null }>>([]);
	const [leaderboardMode, setLeaderboardMode] = useState<"speed" | "rg" | "time">("speed");
	const [leaderboardLoading, setLeaderboardLoading] = useState(false);

	// Click-to-replay state. Clicking a leaderboard row fetches the run and
	// fires a browser-side deterministic replay. The replay banner stays up
	// until the run finishes (poll `isReplayActive`) or the user clicks stop.
	const [replayInfo, setReplayInfo] = useState<{ name: string; value: number; display: string; rank: number } | null>(null);
	const [replayProgress, setReplayProgress] = useState(0); // 0..1
	
	const submittedRef = useRef(false);

	// Track the player's known all-time best so we can auto-submit
	// when the session max surpasses it. Initialized from score_ack.
	const allTimeBestRef = useRef<number>(0);
	// Time challenge: lower is better. 0 = no record yet. Initialized from
	// time_score_ack so the next PR check uses the server-truth value.
	const timeAllTimeBestRef = useRef<number>(0);

	// Memo'd lookup: peer id -> {name, color}. Updated on every room_state.
	const peerInfo = useMemo(() => {
		const m = new Map<string, { name: string; color: readonly [number, number, number] }>();
		if (!room) return m;
		for (const p of room.players) m.set(p.id, { name: p.name, color: p.color });
		return m;
	}, [room]);

	// Boot WASM once when canvas is mounted + room is loaded.
	useEffect(() => {
		if (!canvasRef.current || !room) return;
		let cancelled = false;
		setStatus("loading");

		loadSrModule(canvasRef.current)
			.then(async (mod) => {
				if (cancelled) return;
				const abi = bindCAbi(mod);
				abiRef.current = abi;

				// Expose gamepad polling to C++'s EM_ASM block so it runs
				// at the main loop rate (300 Hz), not at rAF rate.
				(mod as unknown as Record<string, unknown>)._gamepadPoll = (): void => {
					pollGamepads(abi.pushControllerInput);
				};
				abi.setLocalIdentity(
					identity.name || "Player",
					identity.color[0], identity.color[1], identity.color[2],
				);
				GAME_ACTIONS.forEach((action, idx) => abi.setBinding(idx, bindings[action].code));

				if (room.mode === "grapple_challenge") {
					abi.loadChallenge();
				} else if (room.mode === "rg_challenge") {
					abi.loadRgChallenge();
				} else if (room.mode === "time_challenge") {
					abi.loadTimeChallenge();
				} else {
					abi.loadMap(`/maps/${room.mapId}.sr`);
				}
				// Hide the in-game boost meter HUD bar in challenge modes —
				// the leaderboard + session-best already crowd the top of the
				// screen and the bar isn't useful for runs.
				// Hide the boost-meter HUD in any challenge mode — leaderboard
				// + session-best already crowd the top of the screen.
				abi.setVisualShowBoostBar(room.mode === "standard");
				// Show the corridor grid only in RG mode, not time/speed.
				// (Existing per-mode visual setup runs in setVisualRgGrid via
				// the visuals effect below; nothing to do here.)
				// Trail is best-effort — failures must not block the game
				// from starting. If the user picked a custom .srt, load
				// that into the local player's track (id ""); otherwise
				// fall back to the bundled default.
				trailAbiRef.current = bindTrailAbi(mod);
				try {
					if (identity.trail) {
						await loadTrailFromBytes(mod, "", srtBase64ToBytes(identity.trail.b64));
					} else {
						await loadDefaultTrail(mod, "");
					}
				} catch (e) {
					console.warn("[trail] load failed", e);
				}
				if (cancelled) return;
				setStatus("ready");
			})
			.catch((e: unknown) => {
				if (cancelled) return;
				setError(e instanceof Error ? e.message : String(e));
				setStatus("error");
			});

		return () => {
			cancelled = true;
		};
		// We deliberately don't react to identity changes — it's set once
		// when the game boots. The room map is fixed for the session.
		// eslint-disable-next-line react-hooks/exhaustive-deps
	}, [room?.mapId]);

	// Live re-apply bindings whenever the user rebinds — no need to
	// reload the WASM, sr_set_binding just updates the input_map slot.
	useEffect(() => {
		const abi = abiRef.current;
		if (status !== "ready" || !abi) return;
		GAME_ACTIONS.forEach((action, idx) => abi.setBinding(idx, bindings[action].code));
	}, [bindings, status]);

	// Apply the user-chosen render FPS cap to the WASM main loop. Re-runs
	// whenever the slider in OptionsModal moves.
	useEffect(() => {
		const abi = abiRef.current;
		if (status !== "ready" || !abi) return;
		abi.setTargetFps(targetFps);
	}, [targetFps, status]);

	// Push the user's visual palette every time it changes (or once after
	// WASM is ready, with whatever's loaded from localStorage). The C side
	// keeps a single mutable struct, so each setter is just an in-place
	// write — cheap enough to spam on every slider change.
	useEffect(() => {
		const abi = abiRef.current;
		if (status !== "ready" || !abi) return;
		abi.setVisualBg(visuals.bg[0], visuals.bg[1], visuals.bg[2]);
		abi.setVisualWalls(visuals.walls[0], visuals.walls[1], visuals.walls[2]);
		abi.setVisualGrappleStripe(visuals.grappleStripe[0], visuals.grappleStripe[1], visuals.grappleStripe[2]);
		abi.setVisualWallclimbStripe(visuals.wallclimbStripe[0], visuals.wallclimbStripe[1], visuals.wallclimbStripe[2]);
		abi.setVisualGrappleCord(visuals.grappleCord[0], visuals.grappleCord[1], visuals.grappleCord[2]);
		abi.setVisualGrappleHead(visuals.grappleHead[0], visuals.grappleHead[1], visuals.grappleHead[2]);
		abi.setVisualGrappleHeadSize(visuals.grappleHeadSize);
		abi.setVisualBoostSection(visuals.boostSection[0], visuals.boostSection[1], visuals.boostSection[2], visuals.boostSection[3]);
		abi.setVisualBoostPickup(visuals.boostPickup[0], visuals.boostPickup[1], visuals.boostPickup[2], visuals.boostPickup[3]);
		abi.setVisualRgGrid(visuals.showRgGrid);
	}, [visuals, status]);

	// Reset / quicksave / quickload are UI-only keys handled in JS rather
	// than forwarded to GLFW. Capture phase so we can stopImmediatePropagation
	// before the game's keydown listeners see it.
	//
	// We bail if a rebind is in progress (the ControlsModal also listens at
	// capture phase to grab the next keypress) — without that check, F5 would
	// trigger a save while the user was trying to bind it.
	useEffect(() => {
		if (status !== "ready") return;
		const resetCode = bindings.reset.code;
		const saveCode = bindings.save_state.code;
		const loadCode = bindings.load_state.code;
		const onKey = (e: KeyboardEvent): void => {
			if (document.querySelector(".key-cap-active") !== null) return;
			const ae = document.activeElement;
			if (ae instanceof HTMLElement &&
				(ae.tagName === "INPUT" || ae.tagName === "TEXTAREA" || ae.isContentEditable)) {
				return;
			}
			const bind = eventToBinding(e);
			if (bind === null) return;
			const abi = abiRef.current;
			if (!abi) return;
			if (bind.code === resetCode) {
				e.preventDefault();
				e.stopImmediatePropagation();
				if (isRgChallenge) {
					// Reset state. Submission happens automatically on streak
					// break via submit_rg_run, which carries the recorded log
					// for server-side replay validation. Sending a bare
					// submit_rg_score here would be ignored (no backing run
					// for the validator), so we don't.
					submittedRef.current = false;
					abi.resetRgChallenge();
				} else if (isChallenge) {
					// Same here: reset only. submit_run fires on
					// floor-touch-after-airborne and that's the only path
					// that produces a verifiable score.
					submittedRef.current = false;
					abi.resetChallenge();
				} else if (isTimeChallenge) {
					// Time mode: reset returns the player to the left wall
					// and re-arms the recorder. submit_time_run fires only
					// when the right wall is reached after airborne.
					submittedRef.current = false;
					abi.resetTimeChallenge();
				} else {
					abi.resetLocal();
				}
				return;
			}
			// Save/load are disabled in any challenge mode — they'd let a
			// player rewind to the start of a successful section and game
			// the recorder.
			const inAnyChallenge = isChallenge || isRgChallenge || isTimeChallenge;
			if (!inAnyChallenge && bind.code === saveCode) {
				e.preventDefault();
				e.stopImmediatePropagation();
				abi.saveState();
				return;
			}
			if (!inAnyChallenge && bind.code === loadCode) {
				e.preventDefault();
				e.stopImmediatePropagation();
				abi.loadState();
				return;
			}
		};
		window.addEventListener("keydown", onKey, true);
		return () => window.removeEventListener("keydown", onKey, true);
	}, [status, isChallenge, isRgChallenge, isTimeChallenge, bindings.reset.code, bindings.save_state.code, bindings.load_state.code]);

	// Quick chat: number keys 1-4 open menus; pressing 1-4 again while
	// a menu is open sends the selected message (or cancels on 4).
	// Also captured at capture phase so GLFW never sees these keystrokes.
	useEffect(() => {
		if (status !== "ready" || !room) return;

		const onKey = (e: KeyboardEvent): void => {
			// Don't interfere with key rebinding in ControlsModal
			if (document.querySelector(".key-cap-active") !== null) return;
			// Don't fire when typing in a text input
			const ae = document.activeElement;
			if (ae instanceof HTMLElement &&
				(ae.tagName === "INPUT" || ae.tagName === "TEXTAREA" || ae.isContentEditable)) {
				return;
			}

			const code = e.keyCode;
			const isDigit = code >= 49 && code <= 52; // 1-4

			if (!isDigit) {
				// Escape closes any open menu
				if (e.key === "Escape") {
					setQuickChatMenu((prev) => {
						if (prev !== null) {
							e.preventDefault();
							e.stopImmediatePropagation();
						}
						return null;
					});
				}
				return;
			}

			e.preventDefault();
			e.stopImmediatePropagation();

			const num = code - 48; // 1-4

			setQuickChatMenu((prev) => {
				if (prev === null) {
					// Root level: open this menu
					return num;
				}
				// Sub-menu level
				if (num === 4) {
					// Cancel
					return null;
				}
				// Send the message and close
				const slotKey = `${prev}-${num}` as QuickChatSlot;
				const text = quickChat[slotKey];
				if (text && text.trim()) {
					sendChat(text);
				}
				return null;
			});
		};

		window.addEventListener("keydown", onKey, true);
		return () => window.removeEventListener("keydown", onKey, true);
	}, [status, room, quickChat, sendChat]);

	// Broadcast our own .srt blob to the room once the WASM is ready
	// (and again whenever the user picks a different one). Empty body
	// = explicitly cleared; the server caches that too so late joiners
	// don't get a stale blob from a prior session.
	useEffect(() => {
		if (status !== "ready") return;
		const body = identity.trail?.b64 ?? "";
		ws.send({ type: "trail_share", body });
	}, [status, ws, identity.trail]);

	// Push the local snapshot at 30 Hz. Idle until the ABI is ready.
	useEffect(() => {
		if (status !== "ready") return;
		const handle = setInterval(() => {
			const abi = abiRef.current;
			if (!abi) return;
			const bytes = abi.getLocalSnapshot();
			if (!bytes) return;
			ws.send({ type: "snapshot", body: bytesToBase64(bytes) });
		}, SEND_INTERVAL_MS);
		return () => clearInterval(handle);
	}, [status, ws]);

	// Inbound WS handling: snapshots get buffered for interpolation;
	// player_left removes both buffer and ghost. Identity is set/refreshed
	// from room_state's player list whenever it changes — pushGhost stays
	// cheap because it doesn't redo identity.
	const knownIdentitiesRef = useRef<Set<string>>(new Set());
	const ghostBuffersRef = useRef<Map<string, GhostSample[]>>(new Map());
	useEffect(() => {
		if (status !== "ready") return;

		const off = ws.onMessage((msg: ServerMsg) => {
			const abi = abiRef.current;
			if (!abi) return;
			switch (msg.type) {
				case "snapshot": {
					if (msg.playerId === playerId) return;
					// Lazily push identity the first time we see a peer.
					if (!knownIdentitiesRef.current.has(msg.playerId)) {
						const info = peerInfo.get(msg.playerId);
						if (info) {
							abi.setGhostIdentity(msg.playerId, info.name, info.color[0], info.color[1], info.color[2]);
							knownIdentitiesRef.current.add(msg.playerId);
						}
					}
					const decoded = base64ToBytes(msg.body);
					if (decoded.byteLength < SNAPSHOT_BYTES) return;
					const snap = decodeSnapshot(decoded);
					if (!snap) return;
					let buf = ghostBuffersRef.current.get(msg.playerId);
					if (!buf) {
						buf = [];
						ghostBuffersRef.current.set(msg.playerId, buf);
					}
					buf.push({ recvTime: performance.now(), snap });
					if (buf.length > GHOST_BUFFER_MAX) buf.splice(0, buf.length - GHOST_BUFFER_MAX);
					return;
				}
				case "player_left":
					abi.removeGhost(msg.id);
					knownIdentitiesRef.current.delete(msg.id);
					ghostBuffersRef.current.delete(msg.id);
					trailAbiRef.current?.clearTrack(msg.id);
					loadedPeerTrailsRef.current.delete(msg.id);
					return;
				case "trail_share": {
					// A peer published (or cleared) their .srt. Self-shares
					// shouldn't come back to us, but guard anyway.
					if (msg.playerId === playerId) return;
					const tAbi = trailAbiRef.current;
					if (!tAbi) return;
					if (msg.body === "") {
						tAbi.clearTrack(msg.playerId);
						loadedPeerTrailsRef.current.delete(msg.playerId);
						return;
					}
					// Fire-and-forget: trail loading is async but we don't
					// need to block snapshot processing on it. Errors are
					// swallowed (a malformed .srt from a peer must not crash
					// our renderer).
					(async () => {
						try {
							const mod = await loadSrModule(canvasRef.current!);
							await loadTrailFromBytes(mod, msg.playerId, srtBase64ToBytes(msg.body));
							tAbi.setTrackOpacity(msg.playerId, 0.5);
							tAbi.setTrackVisible(msg.playerId, visuals.showGhostTrails);
							loadedPeerTrailsRef.current.add(msg.playerId);
						} catch (e) {
							console.warn(`[trail] failed to load peer trail for ${msg.playerId}`, e);
						}
					})();
					return;
				}
				case "leaderboard": {
					setLeaderboardMode("speed");
					setLeaderboardEntries(msg.entries.map((e) => ({
						rank: e.rank,
						name: e.name,
						value: e.maxSpeed,
						display: String(e.maxSpeed),
						runId: e.runId ?? null,
					})));
					return;
				}
				case "rg_leaderboard": {
					setLeaderboardMode("rg");
					setLeaderboardEntries(msg.entries.map((e) => ({
						rank: e.rank,
						name: e.name,
						value: e.maxStreak,
						display: String(e.maxStreak),
						runId: e.runId ?? null,
					})));
					return;
				}
				case "time_leaderboard": {
					setLeaderboardMode("time");
					setLeaderboardEntries(msg.entries.map((e) => ({
						rank: e.rank,
						name: e.name,
						value: e.durationTicks,
						display: formatTicksAsSeconds(e.durationTicks),
						runId: e.runId ?? null,
					})));
					return;
				}
				case "tp": {
					// Only the named target acts on the teleport; everyone
					// else just gets the announcement so chat could surface
					// it later. We snap our local sim and clear our own
					// ghost buffers (no stale interp samples for ourselves
					// — though we don't render our own ghost, this is just
					// hygiene).
					if (msg.target === playerId) {
						abi.teleportLocal(msg.x, msg.y);
					}
					return;
				}
			}
		});
		return off;
	}, [status, ws, playerId, peerInfo, visuals.showGhostTrails]);

	// Listen for chat-issued /tp commands and turn them into WS messages.
	// We resolve destinations to world coords here so ChatPanel doesn't
	// need to know about ghost buffers / snapshot bytes.
	useEffect(() => {
		if (status !== "ready") return;
		const onCmd = (e: Event): void => {
			const detail = (e as CustomEvent<{ target: string; destId: string }>).detail;
			if (!detail) return;
			const abi = abiRef.current;
			if (!abi) return;

			let dest: { x: number; y: number } | null = null;
			if (detail.destId === playerId) {
				const bytes = abi.getLocalSnapshot();
				if (bytes) {
					const s = decodeSnapshot(bytes);
					if (s) dest = { x: s.posX, y: s.posY };
				}
			} else {
				const buf = ghostBuffersRef.current.get(detail.destId);
				const last = buf?.[buf.length - 1];
				if (last) dest = { x: last.snap.posX, y: last.snap.posY };
			}
			if (!dest) return;
			ws.send({ type: "tp", target: detail.target, x: dest.x, y: dest.y });
		};
		window.addEventListener("sr-cmd-tp", onCmd as EventListener);
		return () => window.removeEventListener("sr-cmd-tp", onCmd as EventListener);
	}, [status, ws, playerId]);

	// Render-rate ghost interpolation. Runs every animation frame, walks
	// the per-peer buffer to find the two samples bracketing
	// (now - INTERP_DELAY_MS), lerps, and pushes the result to WASM. This
	// decouples ghost render fluidity from the network rate — even at
	// 30Hz sends a 120Hz monitor sees smooth motion.
	useEffect(() => {
		if (status !== "ready") return;
		let raf = 0;
		const step = (): void => {
			const abi = abiRef.current;
			if (!abi) {
				raf = requestAnimationFrame(step);
				return;
			}
			const renderTime = performance.now() - INTERP_DELAY_MS;
			for (const [id, buf] of ghostBuffersRef.current) {
				if (buf.length === 0) continue;
				let s: DecodedSnapshot;
				if (buf.length === 1 || renderTime <= buf[0]!.recvTime) {
					s = buf[0]!.snap;
				} else if (renderTime >= buf[buf.length - 1]!.recvTime) {
					// Buffer underrun — hold latest. Better than extrapolating
					// into a rubber-banding ghost.
					s = buf[buf.length - 1]!.snap;
				} else {
					// Find the bracketing pair and lerp.
					let i = 0;
					while (i < buf.length - 1 && buf[i + 1]!.recvTime <= renderTime) i++;
					const s0 = buf[i]!;
					const s1 = buf[i + 1]!;
					const span = s1.recvTime - s0.recvTime;
					const t = span > 0 ? (renderTime - s0.recvTime) / span : 0;
					s = lerpSample(s0.snap, s1.snap, t);
				}
				abi.pushGhost(
					id,
					s.posX, s.posY,
					s.velX, s.velY,
					s.facing, s.anim,
					s.grappleActive ? 1 : 0,
					s.grappleOriginX, s.grappleOriginY,
					s.grappleAttachX, s.grappleAttachY,
					s.grappleLength, s.grappleTaut ? 1 : 0,
					s.sizeX, s.sizeY,
				);
			}
			raf = requestAnimationFrame(step);
		};
		raf = requestAnimationFrame(step);
		return () => cancelAnimationFrame(raf);
	}, [status]);

	// React to the "show other players' trails" toggle: walk every peer
	// whose trail we've loaded and flip its visibility flag in the
	// renderer. Cheap (one ABI call per peer) and only fires on toggle.
	useEffect(() => {
		if (status !== "ready") return;
		const tAbi = trailAbiRef.current;
		if (!tAbi) return;
		for (const peerId of loadedPeerTrailsRef.current) {
			tAbi.setTrackVisible(peerId, visuals.showGhostTrails);
		}
	}, [status, visuals.showGhostTrails]);

	// Refresh known peer identities when the room roster changes (e.g. a
	// late joiner picked a new color). push_ghost doesn't carry identity,
	// so we re-call set_ghost_identity here.
	useEffect(() => {
		if (status !== "ready" || !room) return;
		const abi = abiRef.current;
		if (!abi) return;
		for (const p of room.players) {
			if (p.id === playerId) continue;
			abi.setGhostIdentity(p.id, p.name, p.color[0], p.color[1], p.color[2]);
			knownIdentitiesRef.current.add(p.id);
		}
	}, [room, status, playerId]);

	// Per-frame: hover-label hit test (cursor-only), local-player speed
	// readout, and FPS counter. The hover label still tracks the player's
	// screen position (and naturally lags one frame behind the canvas), but
	// the speedometer is a single fixed-position element so any rAF/sim
	// timing skew can't make it visibly drift relative to the player.
	const speedometerEnabled = visuals.speedometer !== "off";
	useEffect(() => {
		if (status !== "ready" || !room) return;
		let raf = 0;
		const tick = (): void => {
			const abi = abiRef.current;
			if (!abi) {
				raf = requestAnimationFrame(tick);
				return;
			}

			// Local-player speed (rounded to int — the readout is whole-px).
			if (speedometerEnabled || isChallenge || isTimeChallenge) {
				const bytes = abi.getLocalSnapshot();
				if (bytes) {
					const snap = decodeSnapshot(bytes);
					if (snap) {
						const s = Math.round(Math.hypot(snap.velX, snap.velY));
						setLocalSpeed((prev) => (prev === s ? prev : s));
					}
				}
			} else if (localSpeed !== null) {
				setLocalSpeed(null);
			}

			// Challenge mode: poll the C++ side's session max.
			if (isChallenge) {
				const maxSp = Math.round(abi.getMaxSpeed());
				setSessionMax((prev) => (prev === maxSp ? prev : maxSp));

				// Drain any run that just ended (grounded-and-not-swinging
				// for 0.25s). On a PR, fire submit_run with the input
				// stream + savestate; the server replays + inserts the
				// score once verified.
				const finished = abi.consumeFinishedRun();
				if (finished !== null) {
					const speedRounded = Math.round(finished.maxSpeed);
					if (speedRounded > 0 && speedRounded > allTimeBestRef.current) {
						allTimeBestRef.current = speedRounded;
						submittedRef.current = true;
						ws.send({
							type: "submit_run",
							claimedMaxSpeed: speedRounded,
							durationTicks: finished.durationTicks,
							simVersion: abi.runSimVersion(),
							inputs: bytesToBase64(finished.inputs),
							savestate: bytesToBase64(finished.savestate),
						});
					}
				}
			}

			// RG Challenge: poll consecutive count + session best.
			if (isRgChallenge) {
				const rg = Math.round(abi.getRgConsecutive());
				setRgConsecutive((prev) => (prev === rg ? prev : rg));
				const best = Math.round(abi.getRgBest());
				setRgBest((prev) => (prev === best ? prev : best));

				// Drain any RG run that just ended. C++ fires the
				// finished flag on counter→0 OR ground-touch (whichever
				// comes first — see playground::update). JS just submits
				// the PR-beating runs.
				const finished = abi.consumeFinishedRun();
				if (finished !== null && finished.maxStreak > 0
					&& finished.maxStreak > allTimeBestRef.current) {
					allTimeBestRef.current = finished.maxStreak;
					ws.send({
						type: "submit_rg_run",
						claimedMaxStreak: finished.maxStreak,
						durationTicks: finished.durationTicks,
						simVersion: abi.runSimVersion(),
						inputs: bytesToBase64(finished.inputs),
						savestate: bytesToBase64(finished.savestate),
					});
				}
			}

			// Time Challenge: live elapsed-tick readout + PR-gated submit.
			if (isTimeChallenge) {
				const elapsed = abi.getTimeRunElapsedTicks();
				setTimeElapsed((prev) => (prev === elapsed ? prev : elapsed));

				// Drain finished run (C++ fires on right-wall touch after
				// airborne). durationTicks IS the score in this mode.
				const finished = abi.consumeFinishedRun();
				if (finished !== null && finished.durationTicks > 0) {
					const ticks = finished.durationTicks;
					const prior = timeAllTimeBestRef.current;
					const isPR = prior === 0 || ticks < prior;
					if (isPR) {
						timeAllTimeBestRef.current = ticks;
						ws.send({
							type: "submit_time_run",
							claimedDurationTicks: ticks,
							durationTicks: ticks,
							simVersion: abi.runSimVersion(),
							inputs: bytesToBase64(finished.inputs),
							savestate: bytesToBase64(finished.savestate),
						});
					}
					// Auto-reset so the player can immediately attempt
					// another run. The recorder re-arms in
					// waiting-for-input mode (C++ side), so the timer
					// won't tick until the player presses a key.
					abi.resetTimeChallenge();
					setTimeElapsed(0);
				}
			}

			// Hover hit-test only when the cursor is over the canvas.
			const cursor = cursorRef.current;
			if (cursor === null) {
				if (hovered !== null) setHovered(null);
				raf = requestAnimationFrame(tick);
				return;
			}
			let bestId: string | null = null;
			let bestDistSq = HOVER_RADIUS_SQ;
			let bestX = 0, bestY = 0;
			for (const p of room.players) {
				const isLocal = p.id === playerId;
				const pos = abi.getPlayerScreenPos(isLocal ? "" : p.id);
				if (!pos) continue;
				const dx = pos.x - cursor.x;
				const dy = pos.y + 12 - cursor.y;
				const dsq = dx * dx + dy * dy;
				if (dsq < bestDistSq) {
					bestDistSq = dsq;
					bestId = p.id;
					bestX = pos.x;
					bestY = pos.y;
				}
			}
			if (bestId === null) {
				if (hovered !== null) setHovered(null);
			} else {
				const info = peerInfo.get(bestId);
				if (info) {
					const next: HoveredLabel = {
						id: bestId,
						name: info.name,
						color: rgbToCss(info.color, 1),
						x: bestX,
						y: bestY,
					};
					if (
						hovered === null ||
						hovered.id !== next.id ||
						hovered.x !== next.x ||
						hovered.y !== next.y
					) {
						setHovered(next);
					}
				}
			}

			raf = requestAnimationFrame(tick);
		};
		raf = requestAnimationFrame(tick);
		return () => cancelAnimationFrame(raf);
	}, [status, room, peerInfo, playerId, hovered, speedometerEnabled, localSpeed, isChallenge, isRgChallenge, isTimeChallenge, ws]);

	// Clear controller bits when the page loses visibility (tab hidden).
	// Normal polling is driven by the C++ main loop via EM_ASM, which
	// runs at the same rate as tick_frame (300 Hz by default).
	useEffect(() => {
		if (status !== "ready") return;
		const abi = abiRef.current;
		if (!abi) return;

		const clearAll = (): void => {
			for (let i = 0; i < 8; i++) abi.pushControllerInput(i, 0);
		};

		const onHidden = (): void => {
			if (document.visibilityState === "hidden") clearAll();
		};
		document.addEventListener("visibilitychange", onHidden);

		return () => {
			document.removeEventListener("visibilitychange", onHidden);
			clearAll();
		};
	}, [status]);

	// Block default browser actions for game keys (space scrolling, etc).
	useEffect(() => {
		if (status !== "ready") return;
		const handler = (e: KeyboardEvent): void => {
			if ([" ", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight"].includes(e.key)) {
				e.preventDefault();
			}
		};
		window.addEventListener("keydown", handler, { passive: false });
		return () => window.removeEventListener("keydown", handler);
	}, [status]);

	// ── Challenge leaderboard helpers ──────────────────────────────────

	

	

	// Listen for rg_score_ack (server confirms RG score).
	useEffect(() => {
		return ws.onMessage((msg: ServerMsg) => {
			if (msg.type === "rg_score_ack") {
				setScoreAck({ rank: msg.rank, dailyBest: msg.dailyBest });
				if (msg.dailyBest > allTimeBestRef.current) {
					allTimeBestRef.current = msg.dailyBest;
				}
				submittedRef.current = false;
			}
		});
	}, [ws]);

	// Listen for time_score_ack (server confirms a verified time PR).
	// Lower-is-better, so the PR check uses < not >.
	useEffect(() => {
		return ws.onMessage((msg: ServerMsg) => {
			if (msg.type === "time_score_ack") {
				setTimeScoreAck({ rank: msg.rank, bestTicks: msg.bestTicks });
				if (msg.bestTicks > 0
					&& (timeAllTimeBestRef.current === 0 || msg.bestTicks < timeAllTimeBestRef.current)) {
					timeAllTimeBestRef.current = msg.bestTicks;
				}
				submittedRef.current = false;
			}
		});
	}, [ws]);

	const fetchRgLeaderboard = async (): Promise<void> => {
		setLeaderboardLoading(true);
		try {
			const res = await fetch(
				`${LEADERBOARD_URL.replace("/leaderboard", "/rg-leaderboard")}?limit=10`,
			);
			if (!res.ok) throw new Error(`HTTP ${res.status}`);
			const data = (await res.json()) as Array<{ rank: number; name: string; maxStreak: number; runId?: number | null }>;
			setLeaderboardMode("rg");
			setLeaderboardEntries(data.map((r) => ({ rank: r.rank, name: r.name, value: r.maxStreak, display: String(r.maxStreak), runId: r.runId ?? null })));
		} catch {
			setLeaderboardEntries([]);
		} finally {
			setLeaderboardLoading(false);
		}
	};

	const fetchTimeLeaderboard = async (): Promise<void> => {
		setLeaderboardLoading(true);
		try {
			const res = await fetch(
				`${LEADERBOARD_URL.replace("/leaderboard", "/time-leaderboard")}?limit=10`,
			);
			if (!res.ok) throw new Error(`HTTP ${res.status}`);
			const data = (await res.json()) as Array<{ rank: number; name: string; durationTicks: number; runId?: number | null }>;
			setLeaderboardMode("time");
			setLeaderboardEntries(data.map((r) => ({
				rank: r.rank,
				name: r.name,
				value: r.durationTicks,
				display: formatTicksAsSeconds(r.durationTicks),
				runId: r.runId ?? null,
			})));
		} catch {
			setLeaderboardEntries([]);
		} finally {
			setLeaderboardLoading(false);
		}
	};

	const fetchLeaderboard = useCallback(async () => {
		setLeaderboardLoading(true);
		try {
			setLeaderboardMode("speed");
			const res = await fetch(`${LEADERBOARD_URL}?limit=10`);
			if (!res.ok) throw new Error(`HTTP ${res.status}`);
			const data = (await res.json()) as Array<{ rank: number; name: string; maxSpeed: number; runId?: number | null }>;
			setLeaderboardEntries(data.map((r) => ({ rank: r.rank, name: r.name, value: r.maxSpeed, display: String(r.maxSpeed), runId: r.runId ?? null })));
		} catch {
			setLeaderboardEntries([]);
		} finally {
			setLeaderboardLoading(false);
		}
	}, []);

	// Listen for score_ack from server.
	useEffect(() => {
		const off = ws.onMessage((msg: ServerMsg) => {
			if (msg.type === "score_ack") {
				setScoreAck({ rank: msg.rank, dailyBest: msg.dailyBest });
				if (msg.dailyBest > allTimeBestRef.current) {
					allTimeBestRef.current = msg.dailyBest;
				}
				submittedRef.current = false;
			}
		});
		return off;
	}, [ws]);

	// Auto-fetch leaderboard when entering a challenge room.
	useEffect(() => {
		if (status !== "ready") return;
		if (isChallenge) void fetchLeaderboard();
		if (isRgChallenge) void fetchRgLeaderboard();
		if (isTimeChallenge) void fetchTimeLeaderboard();
		// eslint-disable-next-line react-hooks/exhaustive-deps
	}, [status, isChallenge, isRgChallenge, isTimeChallenge]);

	// (No unmount-time submit. Scores are only minted from
	// floor-touch-after-airborne via submit_run, which carries the input
	// log for server-side replay validation. A bare submit_score with no
	// backing run would be a no-op on the new server anyway.)

	// Stop the active replay and put the local player back in a clean state.
	// We don't try to restore mid-run state — replay clobbers the level and
	// the user gets a fresh challenge to keep things predictable.
	const stopReplay = useCallback(() => {
		const abi = abiRef.current;
		if (!abi) return;
		abi.stopReplay();
		if (isChallenge) abi.resetChallenge();
		else if (isRgChallenge) abi.resetRgChallenge();
		else if (isTimeChallenge) abi.resetTimeChallenge();
		// Reset session-best baselines so the next genuine run by the user
		// can submit its score normally. The replay sim drove max_speed /
		// rg_consecutive on the WASM side; reset*Challenge above zeros them.
		submittedRef.current = false;
		setSessionMax(0);
		setRgConsecutive(0);
		setTimeElapsed(0);
		setReplayInfo(null);
		setReplayProgress(0);
	}, [isChallenge, isRgChallenge, isTimeChallenge]);

	// Begin watching a replay. Fetches the run blob from the server, decodes
	// the base64 input log, and hands it to the WASM replay machinery.
	const startReplayFor = useCallback(async (entry: { rank: number; name: string; value: number; display: string; runId: number | null }) => {
		const abi = abiRef.current;
		if (!abi || entry.runId == null) return;
		const base = LEADERBOARD_URL.replace(/\/leaderboard$/, "");
		const path =
			leaderboardMode === "rg"
				? `/rg-run/${entry.runId}`
				: leaderboardMode === "time"
					? `/time-run/${entry.runId}`
					: `/run/${entry.runId}`;
		try {
			const res = await fetch(`${base}${path}`);
			if (!res.ok) throw new Error(`HTTP ${res.status}`);
			const data = await res.json() as {
				inputs: string;
				durationTicks: number;
				savestate?: string;
			};
			if (typeof data.savestate !== "string" || data.savestate.length === 0) {
				throw new Error("run is missing savestate (legacy run, no longer playable)");
			}
			const inputs = base64ToBytes(data.inputs);
			const savestate = base64ToBytes(data.savestate);
			const mode = leaderboardMode === "rg" ? 1 : leaderboardMode === "time" ? 2 : 0;
			const ok = abi.startReplay(inputs, data.durationTicks, mode, savestate);
			if (!ok) throw new Error("replay_start failed");
			submittedRef.current = true; // suppress auto-submit while replaying
			setReplayInfo({ name: entry.name, value: entry.value, display: entry.display, rank: entry.rank });
			setReplayProgress(0);
		} catch (e) {
			console.warn("[replay] failed to start replay", e);
		}
	}, [leaderboardMode]);

	// Poll the WASM replay state while a replay is active so we can update
	// the progress bar and auto-clear when the run ends.
	useEffect(() => {
		if (!replayInfo) return;
		let raf = 0;
		const tick = (): void => {
			const abi = abiRef.current;
			if (!abi) return;
			if (!abi.isReplayActive()) {
				stopReplay();
				return;
			}
			setReplayProgress(abi.replayProgressPermille() / 1000);
			raf = requestAnimationFrame(tick);
		};
		raf = requestAnimationFrame(tick);
		return () => { cancelAnimationFrame(raf); };
	}, [replayInfo, stopReplay]);

	const onCanvasMouseMove = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
		const canvas = canvasRef.current;
		if (!canvas) return;
		const rect = canvas.getBoundingClientRect();
		// The canvas internal resolution (1280x720) may be larger than its
		// rendered size — scale the cursor into canvas-local pixels so it
		// matches sr_get_player_screen_pos's coordinate space.
		const scaleX = canvas.width / rect.width;
		const scaleY = canvas.height / rect.height;
		cursorRef.current = {
			x: (e.clientX - rect.left) * scaleX,
			y: (e.clientY - rect.top) * scaleY,
		};
	}, []);

	const onCanvasMouseLeave = useCallback(() => {
		cursorRef.current = null;
	}, []);

	return (
		<div className="game-root" ref={containerRef}>
			<canvas
				id="canvas"
				ref={canvasRef}
				width={1280}
				height={720}
				className="game-canvas"
				tabIndex={0}
				onContextMenu={(e) => e.preventDefault()}
				onMouseMove={onCanvasMouseMove}
				onMouseLeave={onCanvasMouseLeave}
			/>
			<div className="game-overlay" aria-hidden>
				{!isChallenge && speedometerEnabled && localSpeed !== null && (
					<div className="speed-readout" style={{ color: speedColor(localSpeed) }}>
						{localSpeed}
					</div>
				)}
				{isChallenge && status === "ready" && (
					<div className="challenge-hud">
						<div className="challenge-speed-row">
							<span className="challenge-current" style={{ color: speedColor(localSpeed ?? 0) }}>
								{localSpeed ?? 0}
							</span>
							<span className="challenge-label">speed</span>
						</div>
						<div className="challenge-max-row">
							<span className="challenge-max-value">{sessionMax}</span>
							<span className="challenge-label">session max</span>
						</div>
						{scoreAck && (
							<div className="challenge-score-ack">
								Rank #{scoreAck.rank} &middot; all-time best {scoreAck.dailyBest}
							</div>
						)}
						
					</div>
				)}
				{isRgChallenge && status === "ready" && (
					<div className="challenge-hud">
						<div className="challenge-speed-row">
							<span className="challenge-current" style={{ color: "#ffcc00" }}>
								{rgConsecutive}
							</span>
							<span className="challenge-label">RG streak</span>
						</div>
						<div className="challenge-max-row">
							<span className="challenge-max-value">{rgBest}</span>
							<span className="challenge-label">session best</span>
						</div>
						{scoreAck && (
							<div className="challenge-score-ack">
								Rank #{scoreAck.rank} &middot; all-time best {scoreAck.dailyBest}
							</div>
						)}

					</div>
				)}
				{isTimeChallenge && status === "ready" && (
					<div className="challenge-hud">
						<div className="challenge-speed-row">
							<span className="challenge-current" style={{ color: "#ffcc33" }}>
								{formatTicksAsSeconds(timeElapsed)}
							</span>
							<span className="challenge-label">elapsed</span>
						</div>
						{timeScoreAck && timeScoreAck.bestTicks > 0 && (
							<div className="challenge-max-row">
								<span className="challenge-max-value">{formatTicksAsSeconds(timeScoreAck.bestTicks)}</span>
								<span className="challenge-label">all-time best</span>
							</div>
						)}
						{timeScoreAck && (
							<div className="challenge-score-ack">
								Rank #{timeScoreAck.rank}
							</div>
						)}
					</div>
				)}
				{hovered && (
					<div
						className="player-label"
						style={{
							color: hovered.color,
							transform: `translate(${hovered.x}px, ${hovered.y - 22}px) translateX(-50%)`,
						}}
					>
						{hovered.name}
					</div>
				)}
				{status === "ready" && <QuickChatMenu activeMenu={quickChatMenu} quickChat={quickChat} playerColor={rgbToCss(identity.color)} />}
				{(isChallenge || isRgChallenge || isTimeChallenge) && leaderboardEntries.length > 0 && (
					<div className="leaderboard-panel">
						<div className="lb-panel-title">All-Time Top 10</div>
						<div className="lb-panel-hint">Click a row to watch the replay</div>
						<table className="lb-panel-table">
							<tbody>
								{leaderboardEntries.slice(0, 10).map((e) => {
									const replayable = e.runId != null;
									const classes = [
										e.name === identity.name ? "lb-panel-me" : "",
										replayable ? "lb-panel-replayable" : "",
									].filter(Boolean).join(" ");
									return (
										<tr
											key={e.rank}
											className={classes}
											onClick={replayable ? () => void startReplayFor(e) : undefined}
											title={replayable ? "Click to watch replay" : "No replay available"}
										>
											<td className="lb-panel-rank">#{e.rank}</td>
											<td className="lb-panel-name">{e.name}</td>
											<td className="lb-panel-value">{e.display}</td>
										</tr>
									);
								})}
							</tbody>
						</table>
					</div>
				)}
				{replayInfo && (
					<div className="replay-banner">
						<div className="replay-banner-title">
							Watching <strong>{replayInfo.name}</strong>'s run
							<span className="replay-banner-rank"> · #{replayInfo.rank}</span>
							<span className="replay-banner-value"> · {replayInfo.display}</span>
						</div>
						<div className="replay-banner-progress">
							<div className="replay-banner-progress-bar" style={{ width: `${replayProgress * 100}%` }} />
						</div>
						<button className="replay-banner-stop" onClick={stopReplay} type="button">Stop</button>
					</div>
				)}
				
			</div>

			{status === "loading" && <div className="game-status">Loading game…</div>}
			{status === "error" && (
				<div className="game-status game-status-error">
					Failed to load game: {error}
				</div>
			)}
		</div>
	);
}
