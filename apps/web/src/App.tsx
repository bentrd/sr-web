import { useEffect, useState } from "react";
import type { ClientMsg, ServerMsg } from "@sr-web/protocol";

type ConnState =
	| { kind: "connecting" }
	| { kind: "open"; rttMs: number | null }
	| { kind: "closed"; reason: string };

const WS_URL = import.meta.env.VITE_WS_URL ?? "ws://localhost:4000/ws";

export function App(): JSX.Element {
	const [state, setState] = useState<ConnState>({ kind: "connecting" });

	useEffect(() => {
		const ws = new WebSocket(WS_URL);
		let pingInterval: number | undefined;

		ws.addEventListener("open", () => {
			setState({ kind: "open", rttMs: null });
			const ping = (): void => {
				const ts = Date.now();
				const msg: ClientMsg = { type: "ping", ts };
				ws.send(JSON.stringify(msg));
			};
			ping();
			pingInterval = window.setInterval(ping, 2000);
		});

		ws.addEventListener("message", (ev) => {
			const msg = JSON.parse(ev.data) as ServerMsg;
			if (msg.type === "pong") {
				setState({ kind: "open", rttMs: Date.now() - msg.ts });
			}
		});

		ws.addEventListener("close", (ev) => {
			setState({ kind: "closed", reason: ev.reason || `code ${ev.code}` });
		});

		ws.addEventListener("error", () => {
			setState({ kind: "closed", reason: "error" });
		});

		return () => {
			if (pingInterval !== undefined) window.clearInterval(pingInterval);
			ws.close();
		};
	}, []);

	return (
		<main>
			<h1>SR-Web</h1>
			<p>Phase 0 bootstrap — verifying WS round-trip to server.</p>
			<dl>
				<dt>Server</dt>
				<dd>
					<code>{WS_URL}</code>
				</dd>
				<dt>Status</dt>
				<dd>
					{state.kind === "connecting" && "connecting…"}
					{state.kind === "open" &&
						`open${state.rttMs !== null ? ` · RTT ${state.rttMs}ms` : ""}`}
					{state.kind === "closed" && `closed (${state.reason})`}
				</dd>
			</dl>
		</main>
	);
}
