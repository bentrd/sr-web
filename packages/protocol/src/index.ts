// Shared message types between web client and Bun server.
// Source of truth for the wire protocol. Bumped on breaking changes.
//
// See AGENTS.md → "Snapshot protocol is a contract" — when snapshot fields
// change, update both this file AND the C++ sr_get_local_snapshot /
// sr_push_ghost signatures in the same commit.

export const PROTOCOL_VERSION = 9;

// Hard cap on the per-run savestate blob (raw bytes). 4 KB is far above
// the actual size produced by playground::capture_savestate (POD struct
// memcpys totalling under 1 KB) but generous enough to absorb future
// state additions without another wire bump.
export const SAVESTATE_MAX_BYTES = 4 * 1024;

export type RGB = readonly [r: number, g: number, b: number];

export type GameMode = "standard" | "grapple_challenge" | "rg_challenge" | "time_challenge";

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
	name: string;       // player name
	color: RGB;
	mapId: string;      // map id for standard mode; empty for grapple_challenge
	mode: GameMode;     // "standard" (load a .sr map) or "grapple_challenge" (procedural corridor)
	// Room-level metadata. Every room has a display name + max-player cap
	// regardless of visibility — `public` is just an observability toggle.
	displayName: string;
	maxPlayers: number; // -1 means unlimited; otherwise an integer >= 2
	public: boolean;    // listed on the homepage when true
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
	mode: GameMode;
	hostId: string;
	players: PlayerInfo[];
	started: boolean;
	displayName: string;
	maxPlayers: number; // -1 = unlimited (mirrors CreateRoom.maxPlayers)
	public: boolean;
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
// Public lobbies (homepage discovery)
// ──────────────────────────────────────────────────────────────────────

// Host-only: flip a room's public/private flag mid-session.
export type SetRoomVisibility = {
	type: "set_room_visibility";
	public: boolean;
};

// Homepage subscribes while it's mounted; the server pushes a fresh
// PublicRoomsList immediately and again whenever a public room is
// created / joined / left / started / made-private / GC'd.
export type SubscribePublicRooms = { type: "subscribe_public_rooms" };
export type UnsubscribePublicRooms = { type: "unsubscribe_public_rooms" };

export type PublicRoomSummary = {
	code: string;
	displayName: string;
	mapId: string;
	mode: GameMode;
	playerCount: number;
	maxPlayers: number;
	started: boolean;
	permanent: boolean;
};

export type PublicRoomsList = {
	type: "public_rooms_list";
	rooms: PublicRoomSummary[];
};

// ──────────────────────────────────────────────────────────────────────
// Phase 4–6 — snapshot relay
// Body is opaque base64 to the server; structure mirrors the C ABI in
// game/src/SR cpp/network/sr_api.cpp ("Snapshot wire layout").
// ──────────────────────────────────────────────────────────────────────

// Fixed-size snapshot, must match k_snapshot_bytes on the C++ side.
// Bumping this is a breaking wire change — bump PROTOCOL_VERSION too.
export const SNAPSHOT_BYTES = 48;

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
	sizeX: 40,
	sizeY: 44,
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
// Per-player .srt trail relay. Each client base64-encodes its picked
// .srt zip and sends it once on game start (and again whenever the
// user picks a different one). The server caches one blob per player
// in the room and replays peers' blobs to a fresh joiner so late
// arrivals see everyone's trail. Cap matches the server validator
// (~285 KB raw → ~384 KB base64).
// ──────────────────────────────────────────────────────────────────────

export type TrailShareIn = {
	type: "trail_share";
	body: string; // base64-encoded .srt zip, or empty string to clear
};

export type TrailShareOut = {
	type: "trail_share";
	playerId: string;
	body: string; // empty string means the peer cleared their trail
};

// ──────────────────────────────────────────────────────────────────────
// Chat commands — server is a dumb relay. The requester picks the
// destination position (usually the current world coords of some peer)
// and the server echoes the message to everyone. The CLIENT whose
// playerId matches `target` calls sr_teleport_local on itself.
// ──────────────────────────────────────────────────────────────────────

export type TpRequest = {
	type: "tp";
	target: string; // playerId of who should move
	x: number;
	y: number;
};

export type TpRelay = {
	type: "tp";
	target: string;
	x: number;
	y: number;
	by: string; // playerId who issued the command
};

// ──────────────────────────────────────────────────────────────────────
// Grapple Challenge leaderboard
// ──────────────────────────────────────────────────────────────────────

export type SubmitScore = {
	type: "submit_score";
	maxSpeed: number; // in wu/s, validated server-side (0 < x < 100000)
};

// Submitted on the speed-mode run-end trigger (grounded-and-not-swinging
// for 0.25s) when the run beats the player's known PR. Each run is its
// own self-contained recording: the input log starts at tick 0 of the
// run, and `savestate` carries the player's full physics pose at the
// moment recording was armed. The server replays from a fresh procedural
// corridor + the savestate, so the verifier exactly reproduces the run
// regardless of where in the session it started (post-resetlap, mid-air
// after a previous run finished, etc.).
export type SubmitRun = {
	type: "submit_run";
	claimedMaxSpeed: number; // wu/s (0 < x < 100000)
	durationTicks: number;   // sim ticks this run lasted (300 ticks/sec)
	simVersion: number;      // bumps when physics/input mapping change
	// Base64-encoded input log for this run only:
	//   [varint tickDelta][uint8 bitmask] [varint tickDelta][uint8 bitmask] ...
	// First entry has tickDelta=0 and seeds the initial input state.
	// Bitmask layout: bit 0=left, 1=right, 2=jump, 3=grapple, 4=slide,
	// 5=boost, 6=item, 7=swap. Capped at 256 KB raw (~341 KB base64).
	inputs: string;
	// Base64-encoded player physics savestate captured at the moment the
	// recorder armed for this run. Format defined by C++
	// playground::capture_savestate (magic 'PSAV', version 1). Capped at
	// SAVESTATE_MAX_BYTES raw.
	savestate: string;
};

export type ScoreAck = {
	type: "score_ack";
	rank: number;     // position on today's leaderboard (1-based)
	dailyBest: number; // this player's best speed today (after submission)
};

export type LeaderboardEntry = {
	rank: number;
	name: string;
	maxSpeed: number;
	// Best matching run id for hover-to-replay. null when no recorded
	// run exists for this entry (legacy scores submitted before the
	// runs table was introduced).
	runId?: number | null;
};

export type Leaderboard = {
	type: "leaderboard";
	date: string; // "YYYY-MM-DD"
	entries: LeaderboardEntry[];
};

// -- RG Challenge leaderboard messages --

export type SubmitRgScore = {
	type: "submit_rg_score";
	maxStreak: number; // best consecutive RGs this session
};

// RG-mode counterpart to SubmitRun. Each run ends on counter→0 OR a
// floor touch (whichever fires first). Self-contained: log + savestate
// describe one run that the server replays in isolation, with the RG
// detector running step-by-step to reproduce the streak count.
export type SubmitRgRun = {
	type: "submit_rg_run";
	claimedMaxStreak: number; // integer (1..99999)
	durationTicks: number;    // sim ticks this run lasted
	simVersion: number;
	inputs: string;           // base64-encoded log (same wire format)
	// Base64-encoded player physics savestate (incl. RgChallengeState)
	// captured when the recorder armed. See SubmitRun.savestate.
	savestate: string;
};

export type RgScoreAck = {
	type: "rg_score_ack";
	rank: number;
	dailyBest: number; // this player's best streak today
};

export type RgLeaderboardRow = {
	rank: number;
	name: string;
	maxStreak: number;
	runId?: number | null;
};

export type RgLeaderboard = {
	type: "rg_leaderboard";
	date: string;
	entries: RgLeaderboardRow[];
};

// ──────────────────────────────────────────────────────────────────────
// Time Challenge leaderboard — lower is better, time in sim ticks
// (300 ticks/sec). Run-end is hitting the right wall after airborne.
// ──────────────────────────────────────────────────────────────────────

export type SubmitTimeRun = {
	type: "submit_time_run";
	claimedDurationTicks: number; // ticks taken to reach the end (1..RUN_DURATION_TICKS_MAX)
	durationTicks: number;        // same value — kept for symmetry with other modes
	simVersion: number;
	inputs: string;
	savestate: string;
};

export type TimeScoreAck = {
	type: "time_score_ack";
	rank: number;       // 1-based
	bestTicks: number;  // this player's all-time best (lowest) ticks; 0 if none
};

export type TimeLeaderboardRow = {
	rank: number;
	name: string;
	durationTicks: number; // lower = better
	runId?: number | null;
};

export type TimeLeaderboard = {
	type: "time_leaderboard";
	date: string;
	entries: TimeLeaderboardRow[];
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
	| SnapshotIn
	| TrailShareIn
	| TpRequest
	| SetRoomVisibility
	| SubscribePublicRooms
	| UnsubscribePublicRooms
	| SubmitScore
	| SubmitRun
	| SubmitRgScore
	| SubmitRgRun
	| SubmitTimeRun;

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
		| "room_full"
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
	| TrailShareOut
	| TpRelay
	| PublicRoomsList
	| ErrorMsg
	| ScoreAck
	| Leaderboard
	| RgScoreAck
	| RgLeaderboard
	| TimeScoreAck
	| TimeLeaderboard;
