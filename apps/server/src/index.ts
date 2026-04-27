import type { ServerWebSocket } from "bun";
import type { ClientMsg, GameMode, RGB, ServerMsg } from "@sr-web/protocol";
import { RoomStore, playerInfo, type WsData } from "./rooms";
import { normaliseCode } from "./codes";
import {
	submitScore,
	getAllTimeLeaderboard,
	allTimeBestForPlayer,
	allTimeRankForPlayer,
	submitRgScore,
	getRgAllTimeLeaderboard,
	rgAllTimeBestForPlayer,
	rgAllTimeRankForPlayer,
	submitRun,
	markRunVerified,
	submitRgRun,
	markRgRunVerified,
	getRunById,
	getRgRunById,
} from "./leaderboard";
import { handleAdminRequest } from "./admin";
import { replayRun, speedMatches, replayRgRun, streakMatches } from "./replay";

const PORT = Number(process.env.PORT ?? 4000);

const CORS_HEADERS: Record<string, string> = {
	"access-control-allow-origin": "*",
	"access-control-allow-methods": "GET, POST, OPTIONS",
	"access-control-allow-headers": "content-type",
};

function corsResponse(body: string | null, init?: ResponseInit): Response {
	return new Response(body, {
		...init,
		headers: { ...CORS_HEADERS, ...init?.headers },
	});
}

// Hard cap on a single .srt blob's base64 size. Real workshop trails
// run 30–200 KB (raw ZIP); we allow ~285 KB raw → ~384 KB base64 to
// give headroom for animated trails with bigger sprite sheets.
const TRAIL_B64_MAX = 384 * 1024;
const SCORE_SUBMIT_COOLDOWN_MS = 10_000;
const SCORE_MAX_SPEED_CAP = 100_000;
// Raw input log cap (bytes, before base64). Mirrors run_recorder::k_log_max_bytes
// in C++ — both sides reject anything larger.
const RUN_INPUT_RAW_MAX = 256 * 1024;
const RUN_INPUT_B64_MAX = Math.ceil((RUN_INPUT_RAW_MAX * 4) / 3) + 16;
const RUN_DURATION_TICKS_MAX = 5 * 3600 * 300; // 5 h at 300 Hz

const store = new RoomStore();

// ── Permanent challenge rooms ───────────────────────────────────────────
// These survive server restarts and appear in the public room list so
// anyone can jump into a challenge without creating a room first.
store.createPermanentRoom("SPEED", "grapple_challenge", {
	displayName: "Speed Challenge",
	maxPlayers: -1,
	isPublic: true,
	mode: "grapple_challenge",
});
store.createPermanentRoom("RGCH1", "rg_challenge", {
	displayName: "RG Challenge",
	maxPlayers: -1,
	isPublic: true,
	mode: "rg_challenge",
});

// Rate-limit: playerId → last submit timestamp
const scoreCooldowns = new Map<string, number>();

function send(ws: ServerWebSocket<WsData>, msg: ServerMsg): void {
	ws.send(JSON.stringify(msg));
}

function nextPlayerId(): string {
	return `p_${Math.random().toString(36).slice(2, 10)}`;
}

const VALID_PLAYER_ID = /^p_[a-z0-9]{4,16}$/;
function validResumeId(s: unknown): s is string {
	return typeof s === "string" && VALID_PLAYER_ID.test(s);
}

function chatSystem(roomCode: string, text: string): void {
	const room = store.getRoom(roomCode);
	if (!room) return;
	store.broadcast(room, {
		type: "chat",
		kind: "system",
		playerId: "",
		name: "",
		color: [0.6, 0.6, 0.6],
		text,
		ts: Date.now(),
	});
}

function validName(s: unknown): s is string {
	return typeof s === "string" && s.trim().length > 0 && s.length <= 24;
}

function validColor(c: unknown): c is RGB {
	return (
		Array.isArray(c) &&
		c.length === 3 &&
		c.every(
			(v) => typeof v === "number" && Number.isFinite(v) && v >= 0 && v <= 1,
		)
	);
}

function validMode(m: unknown): m is GameMode {
	return m === "standard" || m === "grapple_challenge" || m === "rg_challenge";
}

function todayDate(): string {
	return new Date().toISOString().slice(0, 10);
}

const server = Bun.serve<WsData, never>({
	port: PORT,

	async fetch(req, server) {
		const url = new URL(req.url);

		const adminRes = await handleAdminRequest(req, url, server);
		if (adminRes) return adminRes;

		if (url.pathname === "/ws") {
			const ok = server.upgrade(req, {
				data: { playerId: nextPlayerId(), roomCode: null, helloSeen: false } satisfies WsData,
			});
			if (ok) return;
			return new Response("upgrade failed", { status: 400 });
		}

		if (url.pathname === "/health") return corsResponse("ok");

		if (url.pathname === "/leaderboard" && req.method === "GET") {
			const limit = Math.min(100, Math.max(1, Number(url.searchParams.get("limit") ?? 10)));
			try {
				const entries = getAllTimeLeaderboard(limit);
				return corsResponse(JSON.stringify(entries), {
					headers: { "content-type": "application/json", "cache-control": "public, max-age=30" },
				});
			} catch {
				return corsResponse(JSON.stringify([]), {
					status: 500,
					headers: { "content-type": "application/json" },
				});
			}
		}

		if (url.pathname === "/rg-leaderboard" && req.method === "GET") {
			const limit = Math.min(100, Math.max(1, Number(url.searchParams.get("limit") ?? 10)));
			const entries = getRgAllTimeLeaderboard(limit);
			return corsResponse(JSON.stringify(entries), {
				status: 200,
				headers: { "Content-Type": "application/json" },
			});
		}

		// Public replay fetch. Returns the recorded input log + metadata so
		// any client can rehydrate a run via sr_replay_start. No auth: a
		// run blob is just `inputs to play back` — there's nothing in it
		// that isn't already implied by the leaderboard entry's existence.
		const runMatch = url.pathname.match(/^\/run\/(\d+)$/);
		if (runMatch && req.method === "GET") {
			const id = Number(runMatch[1]);
			const run = getRunById(id);
			if (!run) {
				return corsResponse(JSON.stringify({ error: "not_found" }), {
					status: 404,
					headers: { "content-type": "application/json" },
				});
			}
			return corsResponse(JSON.stringify({
				id: run.id,
				playerName: run.playerName,
				claimedMaxSpeed: run.claimedMaxSpeed,
				durationTicks: run.durationTicks,
				simVersion: run.simVersion,
				verified: run.verified,
				timestamp: run.timestamp,
				mode: "grapple_challenge",
				inputs: Buffer.from(run.inputs).toString("base64"),
			}), {
				headers: { "content-type": "application/json", "cache-control": "public, max-age=300" },
			});
		}

		const rgRunMatch = url.pathname.match(/^\/rg-run\/(\d+)$/);
		if (rgRunMatch && req.method === "GET") {
			const id = Number(rgRunMatch[1]);
			const run = getRgRunById(id);
			if (!run) {
				return corsResponse(JSON.stringify({ error: "not_found" }), {
					status: 404,
					headers: { "content-type": "application/json" },
				});
			}
			return corsResponse(JSON.stringify({
				id: run.id,
				playerName: run.playerName,
				claimedMaxStreak: run.claimedMaxStreak,
				durationTicks: run.durationTicks,
				simVersion: run.simVersion,
				verified: run.verified,
				timestamp: run.timestamp,
				mode: "rg_challenge",
				inputs: Buffer.from(run.inputs).toString("base64"),
			}), {
				headers: { "content-type": "application/json", "cache-control": "public, max-age=300" },
			});
		}

		if (req.method === "OPTIONS") {
			return corsResponse(null, { status: 204 });
		}

		return corsResponse("sr-web server", { status: 200 });
	},

	websocket: {
		open(ws) {
			// Welcome happens after the client's `hello` so it carries the
			// id the server actually adopted (which may be a resume id from
			// a prior session). If no hello arrives within 1s, send the
			// provisional id so old clients still work.
			setTimeout(() => {
				if (!ws.data.helloSeen) {
					ws.data.helloSeen = true;
					send(ws, { type: "welcome", playerId: ws.data.playerId });
				}
			}, 1000);
		},

		message(ws, raw) {
			let msg: ClientMsg;
			try {
				msg = JSON.parse(typeof raw === "string" ? raw : raw.toString());
			} catch {
				return send(ws, {
					type: "error",
					code: "bad_json",
					message: "invalid JSON",
				});
			}

			switch (msg.type) {
				case "hello": {
					if (ws.data.helloSeen) return;
					ws.data.helloSeen = true;
					if (validResumeId(msg.playerId)) ws.data.playerId = msg.playerId;
					return send(ws, { type: "welcome", playerId: ws.data.playerId });
				}

				case "ping":
					return send(ws, { type: "pong", ts: msg.ts, serverTs: Date.now() });

				case "create_room": {
					if (
						!validName(msg.name) ||
						!validColor(msg.color) ||
						typeof msg.mapId !== "string" ||
						!validMode(msg.mode) ||
						(!msg.mapId.length && msg.mode === "standard") ||
						typeof msg.displayName !== "string" ||
						msg.displayName.trim().length === 0 ||
						msg.displayName.trim().length > 48 ||
						typeof msg.maxPlayers !== "number" ||
						!Number.isInteger(msg.maxPlayers) ||
						(msg.maxPlayers !== -1 && msg.maxPlayers < 2) ||
						msg.maxPlayers > 64 ||
						typeof msg.public !== "boolean"
					) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "name, color, mapId, mode, displayName, maxPlayers, and public required",
						});
					}
					const player = {
						id: ws.data.playerId,
						name: msg.name.trim(),
						color: msg.color,
						ws,
					};
					const room = store.createRoom(player, msg.mapId, {
						displayName: msg.displayName.trim(),
						maxPlayers: msg.maxPlayers,
						isPublic: msg.public,
						mode: msg.mode,
					});
					ws.data.roomCode = room.code;
					return send(ws, store.roomStateMsg(room));
				}

				case "join_room": {
					if (!validName(msg.name) || !validColor(msg.color) || typeof msg.code !== "string") {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "code, name, and color required",
						});
					}
					const code = normaliseCode(msg.code);
					const targetRoom = store.getRoom(code);
					const wasMember = targetRoom?.players.has(ws.data.playerId) ?? false;
					// Dedupe name BEFORE join — `/tp <name>` and the player list
					// rely on names being unique per room.
					const requestedName = msg.name.trim();
					const finalName = targetRoom
						? store.uniqueNameForRoom(targetRoom, requestedName, ws.data.playerId)
						: requestedName;
					const player = {
						id: ws.data.playerId,
						name: finalName,
						color: msg.color,
						ws,
					};
					const result = store.joinRoom(code, player);
					if (result === "not_found") {
						return send(ws, {
							type: "error",
							code: "room_not_found",
							message: `no room with code ${code}`,
						});
					}
					if (result === "full") {
						return send(ws, {
							type: "error",
							code: "room_full",
							message: `room ${code} is full`,
						});
					}
					ws.data.roomCode = result.code;
					// Auto-subscribe to the challenge leaderboard pubsub topic
					// so this client receives live leaderboard updates.
					if (result.mode === "grapple_challenge" || result.mode === "rg_challenge") {
						ws.unsubscribe("leaderboard-speed");
						ws.unsubscribe("leaderboard-rg");
						if (result.mode === "grapple_challenge") {
							ws.subscribe("leaderboard-speed");
						} else {
							ws.subscribe("leaderboard-rg");
						}
					}
					// Tell everyone in the room (including the new joiner) the new state.
					store.broadcast(result, store.roomStateMsg(result));
					// Permanent rooms auto-start when someone joins — broadcast explicitly.
					if (result.permanent && !result.started) {
						store.broadcast(result, { type: "game_started" });
						chatSystem(result.code, "Game started");
					}
					if (!wasMember) {
						store.broadcast(result, {
							type: "player_joined",
							player: playerInfo(player),
						}, player.id);
						chatSystem(result.code, `${player.name} joined`);
					}
					// Replay each other player's cached .srt blob so the
					// joiner sees everyone's trail without waiting for their
					// next broadcast. The joiner's own trail (if any) is
					// excluded — they own the source of truth for their own.
					for (const peer of store.peerTrails(result, player.id)) {
						send(ws, {
							type: "trail_share",
							playerId: peer.playerId,
							body: peer.body,
						});
					}
					return;
				}

				case "leave_room": {
					if (!ws.data.roomCode) return;
					const code = ws.data.roomCode;
					const room = store.getRoom(code);
					const leaver = room?.players.get(ws.data.playerId);
					const leaverName = leaver?.name ?? "A player";
					const remaining = store.leaveRoom(ws.data.playerId, code);
					ws.data.roomCode = null;
					ws.unsubscribe("leaderboard-speed");
					ws.unsubscribe("leaderboard-rg");
					if (remaining) {
						store.broadcast(remaining, { type: "player_left", id: ws.data.playerId });
						store.broadcast(remaining, store.roomStateMsg(remaining));
						chatSystem(code, `${leaverName} left`);
					}
					return;
				}

				case "start_game": {
					if (!ws.data.roomCode) {
						return send(ws, {
							type: "error",
							code: "not_in_room",
							message: "join or create a room first",
						});
					}
					const result = store.startGame(ws.data.roomCode, ws.data.playerId);
					if (result === "not_found") {
						return send(ws, {
							type: "error",
							code: "room_not_found",
							message: "your room no longer exists",
						});
					}
					if (result === "not_host") {
						return send(ws, {
							type: "error",
							code: "not_host",
							message: "only the host can start the game",
						});
					}
					store.broadcast(result, { type: "game_started" });
					store.broadcast(result, store.roomStateMsg(result));
					chatSystem(result.code, "Game started");
					return;
				}

				case "chat_send": {
					if (!ws.data.roomCode) return;
					const room = store.getRoom(ws.data.roomCode);
					if (!room) return;
					const me = room.players.get(ws.data.playerId);
					if (!me) return;
					const text = String(msg.text ?? "").slice(0, 240).trim();
					if (!text) return;
					store.broadcast(room, {
						type: "chat",
						kind: "user",
						playerId: me.id,
						name: me.name,
						color: me.color,
						text,
						ts: Date.now(),
					});
					return;
				}

				case "snapshot": {
					if (!ws.data.roomCode) return;
					const room = store.getRoom(ws.data.roomCode);
					if (!room) return;
					// Pure relay: we don't inspect the body.
					store.broadcast(
						room,
						{
							type: "snapshot",
							playerId: ws.data.playerId,
							body: msg.body,
						},
						ws.data.playerId,
					);
					return;
				}

				case "trail_share": {
					if (!ws.data.roomCode) return;
					const room = store.getRoom(ws.data.roomCode);
					if (!room) return;
					if (typeof msg.body !== "string" || msg.body.length > TRAIL_B64_MAX) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "trail_share body must be base64 string under cap",
						});
					}
					// Cache the blob so late joiners can be backfilled, then
					// fan out to peers. Empty string is a "cleared trail"
					// marker — peers should drop the sender's trail track.
					store.setPlayerTrail(room, ws.data.playerId, msg.body);
					store.broadcast(
						room,
						{
							type: "trail_share",
							playerId: ws.data.playerId,
							body: msg.body,
						},
						ws.data.playerId,
					);
					return;
				}

				case "set_room_visibility": {
					if (!ws.data.roomCode) {
						return send(ws, {
							type: "error",
							code: "not_in_room",
							message: "join or create a room first",
						});
					}
					if (typeof msg.public !== "boolean") {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "set_room_visibility requires { public: boolean }",
						});
					}
					const result = store.setVisibility(ws.data.roomCode, ws.data.playerId, msg.public);
					if (result === "not_found") {
						return send(ws, {
							type: "error",
							code: "room_not_found",
							message: "your room no longer exists",
						});
					}
					if (result === "not_host") {
						return send(ws, {
							type: "error",
							code: "not_host",
							message: "only the host can change visibility",
						});
					}
					store.broadcast(result, store.roomStateMsg(result));
					return;
				}

				case "subscribe_public_rooms": {
					store.subscribePublic(ws);
					return;
				}

				case "unsubscribe_public_rooms": {
					store.unsubscribePublic(ws);
					return;
				}

				case "tp": {
					if (!ws.data.roomCode) return;
					const room = store.getRoom(ws.data.roomCode);
					if (!room) return;
					if (
						typeof msg.target !== "string" ||
						typeof msg.x !== "number" ||
						typeof msg.y !== "number" ||
						!Number.isFinite(msg.x) ||
						!Number.isFinite(msg.y)
					) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "tp requires { target, x, y }",
						});
					}
					if (!room.players.has(msg.target)) return;
					// Permission: non-hosts can only teleport themselves.
					const isHost = ws.data.playerId === room.hostId;
					const isSelf = ws.data.playerId === msg.target;
					if (!isHost && !isSelf) {
						return send(ws, {
							type: "error",
							code: "not_host",
							message: "only the host can teleport other players",
						});
					}
					// Broadcast to everyone — only the target client acts on it,
					// others get the announcement so chat can show "X tp'd Y".
					store.broadcast(room, {
						type: "tp",
						target: msg.target,
						x: msg.x,
						y: msg.y,
						by: ws.data.playerId,
					});
					return;
				}

				case "submit_score": {
					// Score insertion is now driven by submit_run's replay
					// validation callback — only verified runs land in the
					// scores table. submit_score remains for legacy clients
					// and for the player to sync their current rank, but it
					// no longer writes the DB or broadcasts.
					if (!ws.data.roomCode) return;
					const room = store.getRoom(ws.data.roomCode);
					if (!room || room.mode !== "grapple_challenge") return;
					if (typeof msg.maxSpeed !== "number" || !Number.isFinite(msg.maxSpeed) || msg.maxSpeed <= 0 || msg.maxSpeed > SCORE_MAX_SPEED_CAP) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "maxSpeed must be a positive number",
						});
					}
					const me = room.players.get(ws.data.playerId);
					if (!me) return;
					const rank = allTimeRankForPlayer(me.name);
					const dailyBest = allTimeBestForPlayer(me.name);
					send(ws, {
						type: "score_ack",
						rank,
						dailyBest,
					});
					return;
				}

				case "submit_run": {
					// Phase 1: store the input stream + claimed speed for offline
					// analysis. We don't replay-validate yet — that's Phase 2 once
					// we've collected enough real streams to confirm determinism
					// across platforms. For now this runs alongside submit_score:
					// the legacy path keeps the leaderboard live, this path quietly
					// accumulates evidence.
					if (!ws.data.roomCode) return;
					const room = store.getRoom(ws.data.roomCode);
					if (!room || room.mode !== "grapple_challenge") return;
					const me = room.players.get(ws.data.playerId);
					if (!me) return;

					const m = msg as {
						claimedMaxSpeed: number;
						durationTicks: number;
						simVersion: number;
						inputs: string;
					};
					if (
						typeof m.claimedMaxSpeed !== "number" ||
						!Number.isFinite(m.claimedMaxSpeed) ||
						m.claimedMaxSpeed <= 0 ||
						m.claimedMaxSpeed > SCORE_MAX_SPEED_CAP
					) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "claimedMaxSpeed must be a positive number",
						});
					}
					if (
						typeof m.durationTicks !== "number" ||
						!Number.isInteger(m.durationTicks) ||
						m.durationTicks <= 0 ||
						m.durationTicks > RUN_DURATION_TICKS_MAX
					) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "durationTicks out of range",
						});
					}
					if (typeof m.simVersion !== "number" || !Number.isInteger(m.simVersion) || m.simVersion < 1) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "simVersion required",
						});
					}
					if (typeof m.inputs !== "string" || m.inputs.length === 0 || m.inputs.length > RUN_INPUT_B64_MAX) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "inputs must be base64 string under cap",
						});
					}
					let raw: Uint8Array;
					try {
						raw = Uint8Array.from(atob(m.inputs), (c) => c.charCodeAt(0));
					} catch {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "inputs base64 decode failed",
						});
					}
					if (raw.length === 0 || raw.length > RUN_INPUT_RAW_MAX) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "inputs raw size out of range",
						});
					}

					// No cooldown here — submit_run fires alongside submit_score
					// from the same client event (floor-touch with PR). Sharing
					// the score cooldown would race the two messages. The server
					// still bounds runs by payload size + duration, and the client
					// only sends on PR which is naturally rare.

					let runId = 0;
					try {
						runId = submitRun(
							todayDate(),
							me.name,
							m.claimedMaxSpeed,
							m.durationTicks,
							m.simVersion,
							raw,
						);
					} catch {
						// Storage failures shouldn't break the live game — just drop.
						return;
					}

					// Fire the replay validator without blocking the response.
					// On verdict=1 (claimed speed matches replayed peak within
					// tolerance) we ALSO insert into `scores` + broadcast the
					// updated leaderboard + ack the player. That way only
					// verified runs ever surface — fakes get rejected before
					// they're visible to anyone. On verdict=-1 the run is
					// stamped rejected and the score is silently dropped.
					const playerName = me.name;
					const claimed = Math.round(m.claimedMaxSpeed);
					void replayRun(raw, m.durationTicks).then((res) => {
						if (!res.ok) return;
						const verdict: 1 | -1 = speedMatches(m.claimedMaxSpeed, res.maxSpeed)
							? 1
							: -1;
						try {
							markRunVerified(runId, verdict);
						} catch {
							// DB write failures here are non-fatal.
						}
						if (verdict !== 1) return;
						let isPR = false;
						try {
							isPR = submitScore(todayDate(), playerName, claimed);
						} catch {
							return;
						}
						if (!isPR) return;
						// Broadcast the new leaderboard (with this run's runId
						// already linked) so every subscribed client refreshes
						// in real time without polling.
						const lb = getAllTimeLeaderboard(10);
						server.publish("leaderboard-speed", JSON.stringify({
							type: "leaderboard",
							date: todayDate(),
							entries: lb.map((e) => ({
								rank: e.rank,
								name: e.name,
								maxSpeed: e.maxSpeed,
								runId: e.runId,
							})),
						} satisfies import("@sr-web/protocol").Leaderboard));
						// Send the submitter a fresh ack with their new rank.
						// ws may have closed during validation; send() noops
						// safely on a dead socket.
						try {
							send(ws, {
								type: "score_ack",
								rank: allTimeRankForPlayer(playerName),
								dailyBest: allTimeBestForPlayer(playerName),
							});
						} catch {
							// disconnected between submit + verify — fine.
						}
					}).catch(() => {
						// Background — never throw out of the handler.
					});
					return;
				}

				case "submit_rg_score": {
					// Mirror of submit_score: insertion is now driven by
					// submit_rg_run's replay validation. This handler only
					// returns the player's current standing for legacy
					// clients — no DB write, no broadcast.
					if (!ws.data.roomCode) return;
					const room = store.getRoom(ws.data.roomCode);
					if (!room || room.mode !== "rg_challenge") return;
					const streak = (msg as { maxStreak: number }).maxStreak;
					if (typeof streak !== "number" || !Number.isFinite(streak) || streak <= 0 || streak > 99999) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "maxStreak must be a positive integer",
						});
					}
					const me = room.players.get(ws.data.playerId);
					if (!me) return;
					const rank = rgAllTimeRankForPlayer(me.name);
					const dailyBest = rgAllTimeBestForPlayer(me.name);
					send(ws, { type: "rg_score_ack", rank, dailyBest });
					return;
				}

				case "submit_rg_run": {
					// RG-mode counterpart to submit_run. Stores the input log
					// + claimed max_streak, then kicks off a deterministic
					// replay to flip the verified flag.
					if (!ws.data.roomCode) return;
					const room = store.getRoom(ws.data.roomCode);
					if (!room || room.mode !== "rg_challenge") return;
					const me = room.players.get(ws.data.playerId);
					if (!me) return;

					const m = msg as {
						claimedMaxStreak: number;
						durationTicks: number;
						simVersion: number;
						inputs: string;
					};
					if (
						typeof m.claimedMaxStreak !== "number" ||
						!Number.isInteger(m.claimedMaxStreak) ||
						m.claimedMaxStreak <= 0 ||
						m.claimedMaxStreak > 99999
					) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "claimedMaxStreak must be a positive integer",
						});
					}
					if (
						typeof m.durationTicks !== "number" ||
						!Number.isInteger(m.durationTicks) ||
						m.durationTicks <= 0 ||
						m.durationTicks > RUN_DURATION_TICKS_MAX
					) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "durationTicks out of range",
						});
					}
					if (typeof m.simVersion !== "number" || !Number.isInteger(m.simVersion) || m.simVersion < 1) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "simVersion required",
						});
					}
					if (typeof m.inputs !== "string" || m.inputs.length === 0 || m.inputs.length > RUN_INPUT_B64_MAX) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "inputs must be base64 string under cap",
						});
					}
					let raw: Uint8Array;
					try {
						raw = Uint8Array.from(atob(m.inputs), (c) => c.charCodeAt(0));
					} catch {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "inputs base64 decode failed",
						});
					}
					if (raw.length === 0 || raw.length > RUN_INPUT_RAW_MAX) {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "inputs raw size out of range",
						});
					}

					let rgRunId = 0;
					try {
						rgRunId = submitRgRun(
							todayDate(),
							me.name,
							m.claimedMaxStreak,
							m.durationTicks,
							m.simVersion,
							raw,
						);
					} catch {
						return;
					}

					// Same flow as submit_run: on verdict=1 we insert into
					// rg_scores + broadcast + ack. Only verified streaks ever
					// surface on the leaderboard.
					const playerName = me.name;
					const claimed = Math.round(m.claimedMaxStreak);
					void replayRgRun(raw, m.durationTicks).then((res) => {
						if (!res.ok) return;
						const verdict: 1 | -1 = streakMatches(m.claimedMaxStreak, res.maxStreak)
							? 1
							: -1;
						try {
							markRgRunVerified(rgRunId, verdict);
						} catch {
							// Non-fatal.
						}
						if (verdict !== 1) return;
						let isPR = false;
						try {
							isPR = submitRgScore(todayDate(), playerName, claimed);
						} catch {
							return;
						}
						if (!isPR) return;
						const lb = getRgAllTimeLeaderboard(10);
						server.publish("leaderboard-rg", JSON.stringify({
							type: "rg_leaderboard",
							date: todayDate(),
							entries: lb.map((e) => ({
								rank: e.rank,
								name: e.name,
								maxStreak: e.maxStreak,
								runId: e.runId,
							})),
						} satisfies import("@sr-web/protocol").RgLeaderboard));
						try {
							send(ws, {
								type: "rg_score_ack",
								rank: rgAllTimeRankForPlayer(playerName),
								dailyBest: rgAllTimeBestForPlayer(playerName),
							});
						} catch {
							// disconnected — fine.
						}
					}).catch(() => {});
					return;
				}

				default:
					send(ws, {
						type: "error",
						code: "unimplemented",
						message: `message type "${(msg as { type: string }).type}" not implemented`,
					});
			}
		},

		close(ws) {
			store.unsubscribePublic(ws);
			ws.unsubscribe("leaderboard-speed");
			ws.unsubscribe("leaderboard-rg");
			const code = ws.data.roomCode;
			if (!code) return;
			const leaverName = store.getRoom(code)?.players.get(ws.data.playerId)?.name ?? "A player";
			// Schedule grace-period eviction. The client persists its
			// playerId in localStorage and re-sends it via `hello` on
			// reconnect, which cancels this timer in joinRoom().
			store.scheduleEvict(ws.data.playerId, code, (room) => {
				store.broadcast(room, { type: "player_left", id: ws.data.playerId });
				store.broadcast(room, store.roomStateMsg(room));
				chatSystem(room.code, `${leaverName} disconnected`);
			});
		},
	},
});

console.log(`[server] listening on http://localhost:${server.port}`);
console.log(`[server] ws endpoint: ws://localhost:${server.port}/ws`);
