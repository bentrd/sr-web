import type { ServerWebSocket } from "bun";
import type { PlayerInfo, RGB, RoomState, ServerMsg } from "@sr-web/protocol";
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
};

type Room = {
	code: string;
	mapId: string;
	hostId: string;
	players: Map<string, ServerPlayer>;
	started: boolean;
	createdAt: number;
	lastActivityAt: number;
};

const DISCONNECT_GRACE_MS = 30_000;
const IDLE_ROOM_GC_MS = 10 * 60_000;

export class RoomStore {
	private readonly rooms = new Map<string, Room>();
	// playerId → timer that will evict the player from their room if they
	// don't reconnect within the grace period
	private readonly pendingEvict = new Map<string, ReturnType<typeof setTimeout>>();

	constructor() {
		setInterval(() => this.gcIdleRooms(), 60_000);
	}

	createRoom(player: ServerPlayer, mapId: string): Room {
		const code = generateUniqueCode(new Set(this.rooms.keys()));
		const now = Date.now();
		const room: Room = {
			code,
			mapId,
			hostId: player.id,
			players: new Map([[player.id, player]]),
			started: false,
			createdAt: now,
			lastActivityAt: now,
		};
		// Single-player room — name is trivially unique.
		this.rooms.set(code, room);
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

	joinRoom(code: string, player: ServerPlayer): Room | "not_found" {
		const room = this.rooms.get(code);
		if (!room) return "not_found";
		// We allow joining started rooms — the joining client will run its
		// own sim from the map's initial state and appear as a ghost to
		// the others. Refreshing a tab counts as a rejoin: the same player
		// id slots back into the room and the disconnect-eviction is
		// cancelled.
		this.cancelEvict(player.id);
		room.players.set(player.id, player);
		room.lastActivityAt = Date.now();
		return room;
	}

	startGame(code: string, byPlayerId: string): Room | "not_found" | "not_host" {
		const room = this.rooms.get(code);
		if (!room) return "not_found";
		if (room.hostId !== byPlayerId) return "not_host";
		room.started = true;
		room.lastActivityAt = Date.now();
		return room;
	}

	// Immediate removal — used when the player explicitly leaves.
	leaveRoom(playerId: string, code: string): Room | null {
		const room = this.rooms.get(code);
		if (!room) return null;
		room.players.delete(playerId);
		this.cancelEvict(playerId);
		this.maybeReassignHost(room);
		this.maybeDeleteEmptyRoom(room);
		room.lastActivityAt = Date.now();
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
		if (room.players.size === 0) this.rooms.delete(room.code);
	}

	private gcIdleRooms(): void {
		const now = Date.now();
		for (const [code, room] of this.rooms) {
			if (now - room.lastActivityAt > IDLE_ROOM_GC_MS) {
				console.log(`[rooms] gc idle room ${code}`);
				this.rooms.delete(code);
			}
		}
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
			hostId: room.hostId,
			players: [...room.players.values()].map(playerInfo),
			started: room.started,
		};
	}
}

export function playerInfo(p: { id: string; name: string; color: RGB }): PlayerInfo {
	return { id: p.id, name: p.name, color: p.color };
}
