import type { ClientMsg, ServerMsg } from "@sr-web/protocol";

const PORT = Number(process.env.PORT ?? 4000);

function send(ws: { send: (data: string) => void }, msg: ServerMsg): void {
	ws.send(JSON.stringify(msg));
}

const server = Bun.serve({
	port: PORT,

	fetch(req, server) {
		const url = new URL(req.url);

		if (url.pathname === "/ws") {
			if (server.upgrade(req)) return;
			return new Response("upgrade failed", { status: 400 });
		}

		if (url.pathname === "/health") {
			return new Response("ok");
		}

		return new Response("sr-web server", { status: 200 });
	},

	websocket: {
		open(ws) {
			console.log("[ws] open");
		},

		message(ws, raw) {
			let msg: ClientMsg;
			try {
				msg = JSON.parse(typeof raw === "string" ? raw : raw.toString());
			} catch {
				send(ws, { type: "error", code: "bad_json", message: "invalid JSON" });
				return;
			}

			switch (msg.type) {
				case "ping":
					send(ws, { type: "pong", ts: msg.ts, serverTs: Date.now() });
					return;

				// Lobby + snapshot handlers land in Phase 3 / Phase 6.
				default:
					send(ws, {
						type: "error",
						code: "unimplemented",
						message: `message type "${msg.type}" not implemented yet`,
					});
			}
		},

		close(ws) {
			console.log("[ws] close");
		},
	},
});

console.log(`[server] listening on http://localhost:${server.port}`);
console.log(`[server] ws endpoint: ws://localhost:${server.port}/ws`);
