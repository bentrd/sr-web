// Shared message types between web client and Bun server.
// Source of truth for the wire protocol. Bumped on breaking changes.
//
// See AGENTS.md → "Snapshot protocol is a contract" — when snapshot fields
// change, update both this file AND the C++ sr_get_local_snapshot /
// sr_push_ghost signatures in the same commit.

export const PROTOCOL_VERSION = 1;

export type RGB = readonly [r: number, g: number, b: number];

// ──────────────────────────────────────────────────────────────────────
// Phase 0 — connection liveness
// ──────────────────────────────────────────────────────────────────────

export type ClientHello = { type: "ping"; ts: number };
export type ServerHello = { type: "pong"; ts: number; serverTs: number };

// ──────────────────────────────────────────────────────────────────────
// Phase 3 — lobby & rooms (stubs; flesh out in Phase 3)
// ──────────────────────────────────────────────────────────────────────

export type CreateRoom = {
	type: "create_room";
	name: string;
	color: RGB;
	mapId: string;
};

export type JoinRoom = {
	type: "join_room";
	code: string;
	name: string;
	color: RGB;
};

export type LeaveRoom = { type: "leave_room" };
export type StartGame = { type: "start_game" };

export type PlayerInfo = {
	id: string;
	name: string;
	color: RGB;
};

export type RoomState = {
	type: "room_state";
	code: string;
	mapId: string;
	hostId: string;
	players: PlayerInfo[];
	started: boolean;
};

export type PlayerJoined = { type: "player_joined"; player: PlayerInfo };
export type PlayerLeft = { type: "player_left"; id: string };
export type GameStarted = { type: "game_started" };

// ──────────────────────────────────────────────────────────────────────
// Phase 4–6 — snapshot relay
// Body is opaque base64 to the server; structure defined in C++ side.
// ──────────────────────────────────────────────────────────────────────

export type SnapshotIn = {
	type: "snapshot";
	body: string; // base64-encoded bytes from sr_get_local_snapshot
};

export type SnapshotOut = {
	type: "snapshot";
	playerId: string;
	body: string;
};

// ──────────────────────────────────────────────────────────────────────
// Unions
// ──────────────────────────────────────────────────────────────────────

export type ClientMsg =
	| ClientHello
	| CreateRoom
	| JoinRoom
	| LeaveRoom
	| StartGame
	| SnapshotIn;

export type ServerMsg =
	| ServerHello
	| RoomState
	| PlayerJoined
	| PlayerLeft
	| GameStarted
	| SnapshotOut
	| { type: "error"; code: string; message: string };
