// Shared message types between web client and Bun server.
// Source of truth for the wire protocol. Bumped on breaking changes.
//
// See AGENTS.md → "Snapshot protocol is a contract" — when snapshot fields
// change, update both this file AND the C++ sr_get_local_snapshot /
// sr_push_ghost signatures in the same commit.

export const PROTOCOL_VERSION = 2;

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

// Optional resume — client passes the playerId it remembers from a prior
// session. Server uses it as its assigned id (so refresh keeps the same
// identity in a room). If empty/missing, a fresh id is minted.
export type ClientHelloResume = { type: "hello"; playerId?: string };

export type ChatSend = { type: "chat_send"; text: string };

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

// Chat — kind="user" carries an authored message; kind="system" is for
// join/leave/start announcements (playerId/name/color may be empty).
export type ChatMsg = {
	type: "chat";
	kind: "user" | "system";
	playerId: string;
	name: string;
	color: RGB;
	text: string;
	ts: number;
};

// ──────────────────────────────────────────────────────────────────────
// Phase 4–6 — snapshot relay
// Body is opaque base64 to the server; structure mirrors the C ABI in
// game/src/SR cpp/network/sr_api.cpp ("Snapshot wire layout").
// ──────────────────────────────────────────────────────────────────────

// Fixed-size snapshot, must match k_snapshot_bytes on the C++ side.
// Bumping this is a breaking wire change — bump PROTOCOL_VERSION too.
export const SNAPSHOT_BYTES = 40;

// Field byte offsets — the layout is documented in sr_api.cpp.
// Exposed so the JS encoder/decoder can read/write at fixed positions
// without redefining the schema in two places.
export const SNAPSHOT_OFFSETS = {
	posX: 0,
	posY: 4,
	velX: 8,
	velY: 12,
	facing: 16, // int8
	anim: 17, // uint8
	grappleActive: 18, // uint8 (0/1)
	grappleTaut: 19, // uint8 (0/1)
	grappleOriginX: 20,
	grappleOriginY: 24,
	grappleAttachX: 28,
	grappleAttachY: 32,
	grappleLength: 36,
} as const;

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
	| ClientHelloResume
	| CreateRoom
	| JoinRoom
	| LeaveRoom
	| StartGame
	| ChatSend
	| SnapshotIn;

// Sent once per connection right after WS open so the client knows its
// server-assigned id (used to recognise itself in subsequent room_state
// broadcasts).
export type Welcome = { type: "welcome"; playerId: string };

export type ErrorMsg = {
	type: "error";
	code:
		| "bad_json"
		| "unimplemented"
		| "room_not_found"
		| "room_already_started"
		| "not_in_room"
		| "not_host"
		| "validation";
	message: string;
};

export type ServerMsg =
	| ServerHello
	| Welcome
	| RoomState
	| PlayerJoined
	| PlayerLeft
	| GameStarted
	| ChatMsg
	| SnapshotOut
	| ErrorMsg;
