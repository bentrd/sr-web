import type { ServerWebSocket } from "bun";
import type { ClientMsg, RGB, ServerMsg } from "@sr-web/protocol";
import { RoomStore, playerInfo, type WsData } from "./rooms";
import { normaliseCode } from "./codes";

const PORT = Number(process.env.PORT ?? 4000);

// Hard cap on a single .srt blob's base64 size. Real workshop trails
// run 30–200 KB (raw ZIP); we allow ~285 KB raw → ~384 KB base64 to
// give headroom for animated trails with bigger sprite sheets.
const TRAIL_B64_MAX = 384 * 1024;

const store = new RoomStore();

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

const server = Bun.serve<WsData, never>({
	port: PORT,

	fetch(req, server) {
		const url = new URL(req.url);

		if (url.pathname === "/ws") {
			// Provisional id — overwritten if client sends a `hello` with
			// a previously-assigned id within the grace window.
			const ok = server.upgrade(req, {
				data: { playerId: nextPlayerId(), roomCode: null, helloSeen: false } satisfies WsData,
			});
			if (ok) return;
			return new Response("upgrade failed", { status: 400 });
		}

		if (url.pathname === "/health") return new Response("ok");

		return new Response("sr-web server", { status: 200 });
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
							message: "name, color, mapId, displayName, maxPlayers, and public required",
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
					// Tell everyone in the room (including the new joiner) the new state.
					store.broadcast(result, store.roomStateMsg(result));
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
