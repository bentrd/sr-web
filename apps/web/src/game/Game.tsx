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
import { speedColor } from "../state/visuals";
import { loadDefaultTrail, loadTrailFromBytes, bindTrailAbi } from "./trail/loadTrail";
import { base64ToBytes as srtBase64ToBytes } from "./trail/parseSrt";

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
	saveState: () => void;
	loadState: () => boolean;
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
	const f_save_state = mod.cwrap("sr_save_state", null, []);
	const f_load_state = mod.cwrap("sr_load_state", "number", []);

	// Persistent scratch buffers in WASM heap. Allocated once; freed on
	// page unload. malloc/free are exported but we never hit them more
	// than this so the cost is amortised.
	const snapPtr = mod._malloc(SNAPSHOT_BYTES);
	const xPtr = mod._malloc(4);
	const yPtr = mod._malloc(4);

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
		getRgConsecutive: () => f_rg_consecutive(),
		getRgBest: () => f_rg_best(),
		resetRgChallenge: () => { f_reset_rg_ch(); },
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
		saveState: () => { f_save_state(); },
		loadState: () => (f_load_state() as number) !== 0,
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

export function Game(): JSX.Element {
	const { ws, identity, bindings, targetFps, visuals, room, playerId } = useApp();
	const canvasRef = useRef<HTMLCanvasElement | null>(null);
	const containerRef = useRef<HTMLDivElement | null>(null);
	const abiRef = useRef<CAbi | null>(null);
	const trailAbiRef = useRef<ReturnType<typeof bindTrailAbi> | null>(null);
	const loadedPeerTrailsRef = useRef<Set<string>>(new Set());
	const [status, setStatus] = useState<"loading" | "ready" | "error">("loading");
	const [error, setError] = useState<string | null>(null);
	const [hovered, setHovered] = useState<HoveredLabel | null>(null);
	const [localSpeed, setLocalSpeed] = useState<number | null>(null);
	const [fps, setFps] = useState<number>(0);
	const cursorRef = useRef<{ x: number; y: number } | null>(null);

	// Speed Challenge state
	const isChallenge = room?.mode === "grapple_challenge";
	const isRgChallenge = room?.mode === "rg_challenge";
	const [sessionMax, setSessionMax] = useState<number>(0);
	const [rgConsecutive, setRgConsecutive] = useState<number>(0);
	const [rgBest, setRgBest] = useState<number>(0);
	const [scoreAck, setScoreAck] = useState<{ rank: number; dailyBest: number } | null>(null);
	const [leaderboardEntries, setLeaderboardEntries] = useState<Array<{ rank: number; name: string; value: number }>>([]);
	const [leaderboardMode, setLeaderboardMode] = useState<"speed" | "rg">("speed");
	const [leaderboardLoading, setLeaderboardLoading] = useState(false);
	
	const submittedRef = useRef(false);

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
				abi.setLocalIdentity(
					identity.name || "Player",
					identity.color[0], identity.color[1], identity.color[2],
				);
				GAME_ACTIONS.forEach((action, idx) => abi.setBinding(idx, bindings[action].code));

				if (room.mode === "grapple_challenge") {
					abi.loadChallenge();
				} else if (room.mode === "rg_challenge") {
					abi.loadRgChallenge();
				} else {
					abi.loadMap(`/maps/${room.mapId}.sr`);
				}
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
					abi.resetRgChallenge();
				} else if (isChallenge) {
					abi.resetChallenge();
				} else {
					abi.resetLocal();
				}
				return;
			}
			// Save/load are disabled in Speed Challenge.
			if (!isChallenge && bind.code === saveCode) {
				e.preventDefault();
				e.stopImmediatePropagation();
				abi.saveState();
				return;
			}
			if (!isChallenge && bind.code === loadCode) {
				e.preventDefault();
				e.stopImmediatePropagation();
				abi.loadState();
				return;
			}
		};
		window.addEventListener("keydown", onKey, true);
		return () => window.removeEventListener("keydown", onKey, true);
	}, [status, isChallenge, bindings.reset.code, bindings.save_state.code, bindings.load_state.code]);

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
		let lastFpsUpdate = performance.now();
		let framesSinceUpdate = 0;

		const tick = (): void => {
			framesSinceUpdate++;
			const now = performance.now();
			if (now - lastFpsUpdate >= 500) {
				setFps(Math.round((framesSinceUpdate * 1000) / (now - lastFpsUpdate)));
				framesSinceUpdate = 0;
				lastFpsUpdate = now;
			}

			const abi = abiRef.current;
			if (!abi) {
				raf = requestAnimationFrame(tick);
				return;
			}

			// Local-player speed (rounded to int — the readout is whole-px).
			if (speedometerEnabled || isChallenge) {
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
			}

			// RG Challenge: poll consecutive count + session best.
			if (isRgChallenge) {
				const rg = Math.round(abi.getRgConsecutive());
				setRgConsecutive((prev) => (prev === rg ? prev : rg));
				const best = Math.round(abi.getRgBest());
				setRgBest((prev) => (prev === best ? prev : best));
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
	}, [status, room, peerInfo, playerId, hovered, speedometerEnabled, localSpeed]);

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

	const submitScore = useCallback(() => {
		if (!isChallenge || submittedRef.current) return;
		const abi = abiRef.current;
		if (!abi) return;
		const maxSp = Math.round(abi.getMaxSpeed());
		if (maxSp <= 0) return;
		submittedRef.current = true;
		setSessionMax(maxSp);
		ws.send({ type: "submit_score", maxSpeed: maxSp });
	}, [isChallenge, ws]);

	const submitRgScore = (): void => {
		if (rgBest <= 0) return;
		ws.send({ type: "submit_rg_score", maxStreak: rgBest });
		submittedRef.current = true;
	};

	// Listen for rg_score_ack (server confirms RG score).
	useEffect(() => {
		return ws.onMessage((msg: ServerMsg) => {
			if (msg.type === "rg_score_ack") {
				setScoreAck({ rank: msg.rank, dailyBest: msg.dailyBest });
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
			const data = (await res.json()) as Array<{ rank: number; name: string; maxStreak: number }>;
			setLeaderboardMode("rg");
			setLeaderboardEntries(data.map((r) => ({ rank: r.rank, name: r.name, value: r.maxStreak })));
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
			const data = (await res.json()) as Array<{ rank: number; name: string; maxSpeed: number }>;
			setLeaderboardEntries(data.map((r) => ({ rank: r.rank, name: r.name, value: r.maxSpeed })));
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
			}
		});
		return off;
	}, [ws]);

	// Auto-fetch leaderboard when entering a challenge room.
	useEffect(() => {
		if (status !== "ready") return;
		if (isChallenge) void fetchLeaderboard();
		if (isRgChallenge) void fetchRgLeaderboard();
		// eslint-disable-next-line react-hooks/exhaustive-deps
	}, [status, isChallenge, isRgChallenge]);

	// Auto-submit on unmount in challenge mode.
	useEffect(() => {
		return () => {
			if (isChallenge && !submittedRef.current) {
				const abi = abiRef.current;
				if (abi) {
					const maxSp = Math.round(abi.getMaxSpeed());
					if (maxSp > 0) {
						submittedRef.current = true;
						ws.send({ type: "submit_score", maxSpeed: maxSp });
					}
				}
			}
		};
	}, [isChallenge, ws]);

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
						<div className="challenge-buttons">
							<button
								type="button"
								className="challenge-btn"
								onClick={submitScore}
								disabled={submittedRef.current || sessionMax <= 0}
							>
								{submittedRef.current ? "Submitted" : "Submit Score"}
							</button>
						</div>
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
						<div className="challenge-buttons">
							<button
								type="button"
								className="challenge-btn"
								onClick={submitRgScore}
								disabled={submittedRef.current || rgBest <= 0}
							>
								{submittedRef.current ? "Submitted" : "Submit Score"}
							</button>
						</div>
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
				{(isChallenge || isRgChallenge) && leaderboardEntries.length > 0 && (
					<div className="leaderboard-panel">
						<div className="lb-panel-title">All-Time Top 10</div>
						<table className="lb-panel-table">
							<tbody>
								{leaderboardEntries.slice(0, 10).map((e) => (
									<tr key={e.rank} className={e.name === identity.name ? "lb-panel-me" : ""}>
										<td className="lb-panel-rank">#{e.rank}</td>
										<td className="lb-panel-name">{e.name}</td>
										<td className="lb-panel-value">{e.value}</td>
									</tr>
								))}
							</tbody>
						</table>
					</div>
				)}
				{status === "ready" && <div className="fps-readout">{fps} fps</div>}
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
