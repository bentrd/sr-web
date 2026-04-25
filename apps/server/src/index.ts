import type { ServerWebSocket } from "bun";
import type { ClientMsg, RGB, ServerMsg } from "@sr-web/protocol";
import { RoomStore, playerInfo, type WsData } from "./rooms";
import { normaliseCode } from "./codes";

const PORT = Number(process.env.PORT ?? 4000);

const store = new RoomStore();

function send(ws: ServerWebSocket<WsData>, msg: ServerMsg): void {
	ws.send(JSON.stringify(msg));
}

function nextPlayerId(): string {
	return `p_${Math.random().toString(36).slice(2, 10)}`;
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
			const ok = server.upgrade(req, {
				data: { playerId: nextPlayerId(), roomCode: null } satisfies WsData,
			});
			if (ok) return;
			return new Response("upgrade failed", { status: 400 });
		}

		if (url.pathname === "/health") return new Response("ok");

		return new Response("sr-web server", { status: 200 });
	},

	websocket: {
		open(ws) {
			send(ws, { type: "welcome", playerId: ws.data.playerId });
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
				case "ping":
					return send(ws, { type: "pong", ts: msg.ts, serverTs: Date.now() });

				case "create_room": {
					if (!validName(msg.name) || !validColor(msg.color) || typeof msg.mapId !== "string") {
						return send(ws, {
							type: "error",
							code: "validation",
							message: "name, color, and mapId required",
						});
					}
					const player = {
						id: ws.data.playerId,
						name: msg.name.trim(),
						color: msg.color,
						ws,
					};
					const room = store.createRoom(player, msg.mapId);
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
					const player = {
						id: ws.data.playerId,
						name: msg.name.trim(),
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
					if (result === "already_started") {
						return send(ws, {
							type: "error",
							code: "room_already_started",
							message: "this room has already started a game",
						});
					}
					ws.data.roomCode = result.code;
					// Tell everyone in the room (including the new joiner) the new state.
					store.broadcast(result, store.roomStateMsg(result));
					store.broadcast(result, {
						type: "player_joined",
						player: playerInfo(player),
					}, player.id);
					return;
				}

				case "leave_room": {
					if (!ws.data.roomCode) return;
					const code = ws.data.roomCode;
					const remaining = store.leaveRoom(ws.data.playerId, code);
					ws.data.roomCode = null;
					if (remaining) {
						store.broadcast(remaining, { type: "player_left", id: ws.data.playerId });
						store.broadcast(remaining, store.roomStateMsg(remaining));
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

				default:
					send(ws, {
						type: "error",
						code: "unimplemented",
						message: `message type "${(msg as { type: string }).type}" not implemented`,
					});
			}
		},

		close(ws) {
			const code = ws.data.roomCode;
			if (!code) return;
			// Schedule grace-period eviction. If the player reconnects with
			// the same id (currently we don't, but the hook is here) we cancel.
			store.scheduleEvict(ws.data.playerId, code, (room) => {
				store.broadcast(room, { type: "player_left", id: ws.data.playerId });
				store.broadcast(room, store.roomStateMsg(room));
			});
		},
	},
});

console.log(`[server] listening on http://localhost:${server.port}`);
console.log(`[server] ws endpoint: ws://localhost:${server.port}/ws`);
