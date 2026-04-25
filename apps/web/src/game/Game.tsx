// React host for the WASM game.
//
// Lifecycle:
// 1. Mount canvas, load sr.js, await createSrModule({ canvas })
// 2. Resolve cwrap'd C ABI handles
// 3. sr_set_local_identity + sr_load_map (for the room's chosen map)
// 4. Per-tick (rAF): refresh name overlay positions
// 5. Per 33ms (setInterval): sr_get_local_snapshot → ws.send snapshot
// 6. WS snapshot arrives → sr_set_ghost_identity (first time per peer) +
//    sr_push_ghost
// 7. WS player_left → sr_remove_ghost
// 8. Unmount: stop intervals + rAF, leave the WASM alive (factory caches)

import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { SNAPSHOT_BYTES } from "@sr-web/protocol";
import type { ServerMsg } from "@sr-web/protocol";
import { useApp } from "../state/AppState";
import { rgbToCss } from "../lobby/color";
import { loadSrModule, type SrModule } from "../wasm/loadModule";
import { base64ToBytes, bytesToBase64 } from "./snapshotCodec";

// 30 Hz network send rate. Sim runs at ~300 Hz inside WASM, render at
// monitor refresh — the three are deliberately decoupled (see AGENTS.md).
const SEND_INTERVAL_MS = 33;

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
	) => void;
	setGhostIdentity: (id: string, name: string, r: number, g: number, b: number) => void;
	removeGhost: (id: string) => void;
	getLocalSnapshot: () => Uint8Array | null;
	getPlayerScreenPos: (id: string) => { x: number; y: number } | null;
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
	]);
	const f_set_ghost_id = mod.cwrap("sr_set_ghost_identity", null, ["string", "string", "number", "number", "number"]);
	const f_remove = mod.cwrap("sr_remove_ghost", null, ["string"]);
	const f_get_snap = mod.cwrap("sr_get_local_snapshot", "number", ["number", "number"]);
	const f_get_pos = mod.cwrap("sr_get_player_screen_pos", "number", ["string", "number", "number"]);

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
		pushGhost: (id, posX, posY, velX, velY, facing, anim, grappleActive, gxOrigin, gyOrigin, gxAttach, gyAttach, gLength, gTaut) => {
			f_push(
				id,
				posX, posY,
				velX, velY,
				facing, anim,
				grappleActive,
				gxOrigin, gyOrigin,
				gxAttach, gyAttach,
				gLength, gTaut,
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
	};
}

interface PlayerLabel {
	id: string;
	name: string;
	color: string;
	x: number;
	y: number;
	visible: boolean;
}

// When labels share a screen position (e.g. spawn), stack them vertically
// so each remains readable. Sort by id for stable ordering.
function stackLabels(labels: readonly PlayerLabel[]): readonly (PlayerLabel & { stackOffset: number })[] {
	const byBucket = new Map<string, PlayerLabel[]>();
	for (const l of labels) {
		if (!l.visible) continue;
		const key = `${Math.round(l.x / 8)}:${Math.round(l.y / 8)}`;
		const list = byBucket.get(key) ?? [];
		list.push(l);
		byBucket.set(key, list);
	}
	const out: (PlayerLabel & { stackOffset: number })[] = [];
	for (const list of byBucket.values()) {
		list.sort((a, b) => a.id.localeCompare(b.id));
		list.forEach((l, i) => out.push({ ...l, stackOffset: i * 18 }));
	}
	for (const l of labels) {
		if (!l.visible) out.push({ ...l, stackOffset: 0 });
	}
	return out;
}

export function Game(): JSX.Element {
	const { ws, identity, room, playerId } = useApp();
	const canvasRef = useRef<HTMLCanvasElement | null>(null);
	const containerRef = useRef<HTMLDivElement | null>(null);
	const abiRef = useRef<CAbi | null>(null);
	const [status, setStatus] = useState<"loading" | "ready" | "error">("loading");
	const [error, setError] = useState<string | null>(null);
	const [labels, setLabels] = useState<readonly PlayerLabel[]>([]);

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

	// Inbound WS handling: snapshots → push_ghost; player_left → remove_ghost.
	// Identity is set/refreshed from room_state's player list whenever it
	// changes — push_ghost stays cheap because it doesn't redo identity.
	const knownIdentitiesRef = useRef<Set<string>>(new Set());
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
					const dv = new DataView(decoded.buffer, decoded.byteOffset, decoded.byteLength);
					abi.pushGhost(
						msg.playerId,
						dv.getFloat32(0, true), dv.getFloat32(4, true),
						dv.getFloat32(8, true), dv.getFloat32(12, true),
						dv.getInt8(16), dv.getUint8(17),
						dv.getUint8(18),
						dv.getFloat32(20, true), dv.getFloat32(24, true),
						dv.getFloat32(28, true), dv.getFloat32(32, true),
						dv.getFloat32(36, true), dv.getUint8(19),
					);
					return;
				}
				case "player_left":
					abi.removeGhost(msg.id);
					knownIdentitiesRef.current.delete(msg.id);
					return;
			}
		});
		return off;
	}, [status, ws, playerId, peerInfo]);

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

	// Per-frame name-label refresh. We render N+1 labels (peers + self)
	// using transform: translate so the browser keeps it cheap.
	useEffect(() => {
		if (status !== "ready" || !room) return;
		let raf = 0;
		const ids = [...room.players.map((p) => p.id)];

		const tick = (): void => {
			const abi = abiRef.current;
			if (!abi) {
				raf = requestAnimationFrame(tick);
				return;
			}
			const next: PlayerLabel[] = [];
			for (const id of ids) {
				const info = peerInfo.get(id);
				if (!info) continue;
				const isLocal = id === playerId;
				const pos = abi.getPlayerScreenPos(isLocal ? "" : id);
				next.push({
					id,
					name: info.name,
					color: rgbToCss(info.color, 1),
					x: pos?.x ?? 0,
					y: pos?.y ?? 0,
					visible: pos !== null,
				});
			}
			setLabels(next);
			raf = requestAnimationFrame(tick);
		};
		raf = requestAnimationFrame(tick);
		return () => cancelAnimationFrame(raf);
	}, [status, room, peerInfo, playerId]);

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
			/>
			<div className="game-overlay" aria-hidden>
				{stackLabels(labels).map((l) =>
					l.visible ? (
						<div
							key={l.id}
							className="player-label"
							style={{
								color: l.color,
								transform: `translate(${l.x}px, ${l.y - 22 - l.stackOffset}px) translateX(-50%)`,
							}}
						>
							{l.name}
						</div>
					) : null,
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
