import type { ServerWebSocket } from "bun";
import type { GameMode, PlayerInfo, RGB, RoomState, ServerMsg } from "@sr-web/protocol";
import { generateUniqueCode } from "./codes";

export type WsData = {
	playerId: string;
	roomCode: string | null;
	helloSeen: boolean;
};

type ServerPlayer = {
	id: string;
	name: string;
	color: RGB;
	ws: ServerWebSocket<WsData>;
	// Latest base64-encoded .srt blob this player shared. Empty/missing
	// means they haven't picked a trail yet (or explicitly cleared it).
	// Replayed to fresh joiners so late arrivals see everyone's trail.
	trailB64?: string;
};

export type CreateRoomOptions = {
	displayName: string;
	maxPlayers: number;
	isPublic: boolean;
	mode: GameMode;
};

type Room = {
	code: string;
	mapId: string;
	mode: GameMode;
	hostId: string;
	players: Map<string, ServerPlayer>;
	started: boolean;
	createdAt: number;
	lastActivityAt: number;
	displayName: string;
	maxPlayers: number; // -1 = unlimited
	isPublic: boolean;
	permanent: boolean; // never GC'd, auto-recreated on startup
};

const DISCONNECT_GRACE_MS = 30_000;
const IDLE_ROOM_GC_MS = 10 * 60_000;

export class RoomStore {
	private readonly rooms = new Map<string, Room>();
	// playerId → timer that will evict the player from their room if they
	// don't reconnect within the grace period
	private readonly pendingEvict = new Map<string, ReturnType<typeof setTimeout>>();

	// Sockets currently subscribed to the public rooms list (homepage).
	private readonly publicSubscribers = new Set<ServerWebSocket<WsData>>();

	constructor() {
		setInterval(() => this.gcIdleRooms(), 60_000);
	}

	createRoom(player: ServerPlayer, mapId: string, opts: CreateRoomOptions): Room {
		const code = generateUniqueCode(new Set(this.rooms.keys()));
		const now = Date.now();
		const room: Room = {
			code,
			mapId,
			mode: opts.mode,
			hostId: player.id,
			players: new Map([[player.id, player]]),
			started: false,
			createdAt: now,
			lastActivityAt: now,
			displayName: opts.displayName,
			maxPlayers: opts.maxPlayers,
			isPublic: opts.isPublic,
			permanent: false,
		};
		// Single-player room — name is trivially unique.
		this.rooms.set(code, room);
		if (room.isPublic) this.broadcastPublicList();
		return room;
	}

	// Create a permanent room with a fixed code. Permanent rooms are never
	// GC'd and reappear after server restart. The code must be exactly 5
	// Crockford base32 characters (the alphabet in codes.ts).
	createPermanentRoom(code: string, mapId: string, opts: CreateRoomOptions): Room {
		const now = Date.now();
		const room: Room = {
			code,
			mapId,
			mode: opts.mode,
			hostId: "server", // no real host — first joiner can start
			players: new Map(),
			started: false,
			createdAt: now,
			lastActivityAt: now,
			displayName: opts.displayName,
			maxPlayers: opts.maxPlayers,
			isPublic: opts.isPublic,
			permanent: true,
		};
		this.rooms.set(code, room);
		if (room.isPublic) this.broadcastPublicList();
		console.log(`[rooms] permanent room ${code} (${opts.mode})`);
		return room;
	}

	// Pick a name that doesn't collide with anyone already in the room.
	// `selfId` lets a returning player keep their existing name. Suffixes
	// look like "Bob (2)", "Bob (3)", … bumping until we find a free slot.
	uniqueNameForRoom(room: Room, requested: string, selfId: string): string {
		const taken = new Set<string>();
		for (const p of room.players.values()) {
			if (p.id !== selfId) taken.add(p.name);
		}
		if (!taken.has(requested)) return requested;
		for (let i = 2; i < 1000; i++) {
			const candidate = `${requested} (${i})`;
			if (!taken.has(candidate)) return candidate;
		}
		return `${requested} (${selfId.slice(2, 6)})`;
	}

	getRoom(code: string): Room | undefined {
		return this.rooms.get(code);
	}

	joinRoom(code: string, player: ServerPlayer): Room | "not_found" | "full" {
		const room = this.rooms.get(code);
		if (!room) return "not_found";
		// Capacity check skips returning members (refresh) and unlimited rooms.
		const isReturning = room.players.has(player.id);
		if (
			!isReturning &&
			room.maxPlayers !== -1 &&
			room.players.size >= room.maxPlayers
		) {
			return "full";
		}
		// We allow joining started rooms — the joining client will run its
		// own sim from the map's initial state and appear as a ghost to
		// the others. Refreshing a tab counts as a rejoin: the same player
		// id slots back into the room and the disconnect-eviction is
		// cancelled.
		this.cancelEvict(player.id);
		room.players.set(player.id, player);
		room.lastActivityAt = Date.now();
		// Auto-start permanent challenge rooms as soon as anyone joins.
		if (room.permanent && !room.started) {
			room.started = true;
		}
		if (room.isPublic) this.broadcastPublicList();
		return room;
	}

	startGame(code: string, byPlayerId: string): Room | "not_found" | "not_host" {
		const room = this.rooms.get(code);
		if (!room) return "not_found";
		if (room.hostId !== byPlayerId) return "not_host";
		room.started = true;
		room.lastActivityAt = Date.now();
		if (room.isPublic) this.broadcastPublicList();
		return room;
	}

	// Immediate removal — used when the player explicitly leaves.
	leaveRoom(playerId: string, code: string): Room | null {
		const room = this.rooms.get(code);
		if (!room) return null;
		const wasPublic = room.isPublic;
		room.players.delete(playerId);
		this.cancelEvict(playerId);
		this.maybeReassignHost(room);
		this.maybeDeleteEmptyRoom(room);
		room.lastActivityAt = Date.now();
		if (wasPublic) this.broadcastPublicList();
		return room.players.size > 0 ? room : null;
	}

	// Grace-period removal — used on WS close. If the player reconnects
	// (joins again with the same id) within the grace window we cancel
	// the eviction in joinRoom().
	scheduleEvict(playerId: string, code: string, onEvict: (room: Room) => void): void {
		this.cancelEvict(playerId);
		const t = setTimeout(() => {
			this.pendingEvict.delete(playerId);
			const room = this.leaveRoom(playerId, code);
			if (room) onEvict(room);
		}, DISCONNECT_GRACE_MS);
		this.pendingEvict.set(playerId, t);
	}

	private cancelEvict(playerId: string): void {
		const t = this.pendingEvict.get(playerId);
		if (t) {
			clearTimeout(t);
			this.pendingEvict.delete(playerId);
		}
	}

	private maybeReassignHost(room: Room): void {
		if (room.players.has(room.hostId)) return;
		const next = room.players.values().next().value;
		if (next) room.hostId = next.id;
	}

	private maybeDeleteEmptyRoom(room: Room): void {
		if (room.players.size === 0 && !room.permanent) this.rooms.delete(room.code);
	}

	private gcIdleRooms(): void {
		const now = Date.now();
		let publicChanged = false;
		for (const [code, room] of this.rooms) {
			if (!room.permanent && now - room.lastActivityAt > IDLE_ROOM_GC_MS) {
				console.log(`[rooms] gc idle room ${code}`);
				this.rooms.delete(code);
				if (room.isPublic) publicChanged = true;
			}
		}
		if (publicChanged) this.broadcastPublicList();
	}

	// Broadcast helpers — kept here so the index.ts message handlers stay
	// purely about routing.

	broadcast(room: Room, msg: ServerMsg, exceptPlayerId?: string): void {
		const data = JSON.stringify(msg);
		for (const p of room.players.values()) {
			if (p.id === exceptPlayerId) continue;
			try {
				p.ws.send(data);
			} catch {
				// Socket may be in a closing state; ignore.
			}
		}
	}

	roomStateMsg(room: Room): RoomState {
		return {
			type: "room_state",
			code: room.code,
			mapId: room.mapId,
			mode: room.mode,
			hostId: room.hostId,
			players: [...room.players.values()].map(playerInfo),
			started: room.started,
			displayName: room.displayName,
			maxPlayers: room.maxPlayers,
			public: room.isPublic,
		};
	}

	// Cache the latest .srt blob for a player. Empty string is allowed
	// (means "cleared"); null/undefined would mean "never set" which the
	// peer-replay logic distinguishes by checking trailB64 != null.
	setPlayerTrail(room: Room, playerId: string, b64: string): void {
		const p = room.players.get(playerId);
		if (!p) return;
		p.trailB64 = b64;
		room.lastActivityAt = Date.now();
	}

	// Snapshot of every other player's stored trail blob in this room.
	// Used to replay cached trails to a fresh joiner so they see everyone
	// from frame 1 instead of waiting for each peer's next broadcast.
	peerTrails(room: Room, exceptPlayerId: string): Array<{ playerId: string; body: string }> {
		const out: Array<{ playerId: string; body: string }> = [];
		for (const p of room.players.values()) {
			if (p.id === exceptPlayerId) continue;
			if (typeof p.trailB64 !== "string" || p.trailB64.length === 0) continue;
			out.push({ playerId: p.id, body: p.trailB64 });
		}
		return out;
	}

	// Host-only: toggle a room's public/private flag mid-session.
	setVisibility(code: string, byPlayerId: string, isPublic: boolean):
		| Room
		| "not_found"
		| "not_host"
	{
		const room = this.rooms.get(code);
		if (!room) return "not_found";
		if (room.hostId !== byPlayerId) return "not_host";
		if (room.isPublic === isPublic) return room;
		room.isPublic = isPublic;
		room.lastActivityAt = Date.now();
		this.broadcastPublicList();
		return room;
	}

	publicRoomSummaries() {
		const out = [] as Array<{
			code: string;
			displayName: string;
			mapId: string;
			mode: GameMode;
			playerCount: number;
			maxPlayers: number;
			started: boolean;
			permanent: boolean;
		}>;
		for (const room of this.rooms.values()) {
			if (!room.isPublic) continue;
			out.push({
				code: room.code,
				displayName: room.displayName,
				mapId: room.mapId,
				mode: room.mode,
				playerCount: room.players.size,
				maxPlayers: room.maxPlayers,
				started: room.started,
				permanent: room.permanent,
			});
		}
		return out;
	}

	subscribePublic(ws: ServerWebSocket<WsData>): void {
		this.publicSubscribers.add(ws);
		try {
			ws.send(
				JSON.stringify({
					type: "public_rooms_list",
					rooms: this.publicRoomSummaries(),
				}),
			);
		} catch {
			// Ignore — subscribe is best-effort.
		}
	}

	unsubscribePublic(ws: ServerWebSocket<WsData>): void {
		this.publicSubscribers.delete(ws);
	}

	broadcastPublicList(): void {
		if (this.publicSubscribers.size === 0) return;
		const data = JSON.stringify({
			type: "public_rooms_list",
			rooms: this.publicRoomSummaries(),
		});
		for (const ws of this.publicSubscribers) {
			try {
				ws.send(data);
			} catch {
				// Ignore; they'll be cleaned up on close.
			}
		}
	}
}

export function playerInfo(p: { id: string; name: string; color: RGB }): PlayerInfo {
	return { id: p.id, name: p.name, color: p.color };
}
