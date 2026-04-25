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
import { GAME_ACTIONS } from "../state/bindings";

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
}

function bindCAbi(mod: SrModule): CAbi {
	const f_set_id = mod.cwrap("sr_set_local_identity", null, ["string", "number", "number", "number"]);
	const f_load = mod.cwrap("sr_load_map", null, ["string"]);
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
	const { ws, identity, bindings, room, playerId } = useApp();
	const canvasRef = useRef<HTMLCanvasElement | null>(null);
	const containerRef = useRef<HTMLDivElement | null>(null);
	const abiRef = useRef<CAbi | null>(null);
	const [status, setStatus] = useState<"loading" | "ready" | "error">("loading");
	const [error, setError] = useState<string | null>(null);
	const [hovered, setHovered] = useState<HoveredLabel | null>(null);
	const [fps, setFps] = useState<number>(0);
	// Mouse position in canvas-local pixels (matches sr_get_player_screen_pos
	// output). null when the cursor isn't over the canvas.
	const cursorRef = useRef<{ x: number; y: number } | null>(null);

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
			.then((mod) => {
				if (cancelled) return;
				const abi = bindCAbi(mod);
				abiRef.current = abi;
				abi.setLocalIdentity(
					identity.name || "Player",
					identity.color[0], identity.color[1], identity.color[2],
				);
				GAME_ACTIONS.forEach((action, idx) => abi.setBinding(idx, bindings[action].code));
				abi.loadMap(`/maps/${room.mapId}.sr`);
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
					return;
			}
		});
		return off;
	}, [status, ws, playerId, peerInfo]);

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

	// Per-frame: pick the label nearest to the cursor (within radius), if any,
	// and update the FPS readout. Skips all per-player WASM screen-pos calls
	// when the cursor isn't over the canvas — saves N FFI hops per frame in
	// the common case where the user isn't hunting names.
	useEffect(() => {
		if (status !== "ready" || !room) return;
		let raf = 0;
		const ids = [...room.players.map((p) => p.id)];
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
			const cursor = cursorRef.current;
			if (!abi || !cursor) {
				if (hovered !== null) setHovered(null);
				raf = requestAnimationFrame(tick);
				return;
			}

			let bestId: string | null = null;
			let bestDistSq = HOVER_RADIUS_SQ;
			let bestX = 0, bestY = 0;
			for (const id of ids) {
				const isLocal = id === playerId;
				const pos = abi.getPlayerScreenPos(isLocal ? "" : id);
				if (!pos) continue;
				const dx = pos.x - cursor.x;
				const dy = pos.y + 12 - cursor.y;  // bias y to player center
				const dsq = dx * dx + dy * dy;
				if (dsq < bestDistSq) {
					bestDistSq = dsq;
					bestId = id;
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
	}, [status, room, peerInfo, playerId, hovered]);

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
