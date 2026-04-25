import type { ClientMsg, ServerMsg } from "@sr-web/protocol";

export type WsStatus =
	| { kind: "connecting" }
	| { kind: "open" }
	| { kind: "closed"; reason: string };

export type WsListener = (msg: ServerMsg) => void;
export type StatusListener = (s: WsStatus) => void;

const RECONNECT_BASE_MS = 1000;
const RECONNECT_MAX_MS = 15_000;
const PLAYER_ID_KEY = "sr-web.playerId";

function loadPlayerId(): string | null {
	try {
		return localStorage.getItem(PLAYER_ID_KEY);
	} catch {
		return null;
	}
}

function savePlayerId(id: string): void {
	try {
		localStorage.setItem(PLAYER_ID_KEY, id);
	} catch {
		// localStorage disabled — not fatal, server will mint a new id
	}
}

export class WsClient {
	private ws: WebSocket | null = null;
	private playerId: string | null = loadPlayerId();
	private status: WsStatus = { kind: "connecting" };
	private readonly msgListeners = new Set<WsListener>();
	private readonly statusListeners = new Set<StatusListener>();
	private reconnectAttempt = 0;
	private reconnectTimer: number | null = null;
	private disposed = false;

	constructor(private readonly url: string) {
		this.connect();
	}

	private connect(): void {
		if (this.disposed) return;
		this.setStatus({ kind: "connecting" });
		const ws = new WebSocket(this.url);
		this.ws = ws;

		ws.addEventListener("open", () => {
			this.reconnectAttempt = 0;
			// Send our remembered id so the server resumes our session
			// instead of minting a new one. Empty/null on first ever visit.
			ws.send(JSON.stringify({ type: "hello", playerId: this.playerId ?? undefined }));
			this.setStatus({ kind: "open" });
		});

		ws.addEventListener("message", (ev) => {
			let msg: ServerMsg;
			try {
				msg = JSON.parse(ev.data);
			} catch {
				return;
			}
			if (msg.type === "welcome") {
				this.playerId = msg.playerId;
				savePlayerId(msg.playerId);
			}
			for (const l of this.msgListeners) l(msg);
		});

		ws.addEventListener("close", (ev) => {
			this.setStatus({ kind: "closed", reason: ev.reason || `code ${ev.code}` });
			this.scheduleReconnect();
		});

		ws.addEventListener("error", () => {
			this.setStatus({ kind: "closed", reason: "connection error" });
		});
	}

	private scheduleReconnect(): void {
		if (this.disposed) return;
		if (this.reconnectTimer !== null) return;
		const delay = Math.min(
			RECONNECT_MAX_MS,
			RECONNECT_BASE_MS * 2 ** this.reconnectAttempt,
		);
		this.reconnectAttempt++;
		this.reconnectTimer = window.setTimeout(() => {
			this.reconnectTimer = null;
			this.connect();
		}, delay);
	}

	private setStatus(s: WsStatus): void {
		this.status = s;
		for (const l of this.statusListeners) l(s);
	}

	send(msg: ClientMsg): void {
		if (this.ws?.readyState === WebSocket.OPEN) {
			this.ws.send(JSON.stringify(msg));
		}
	}

	onMessage(l: WsListener): () => void {
		this.msgListeners.add(l);
		return () => this.msgListeners.delete(l);
	}

	onStatus(l: StatusListener): () => void {
		this.statusListeners.add(l);
		l(this.status);
		return () => this.statusListeners.delete(l);
	}

	getStatus(): WsStatus {
		return this.status;
	}

	getPlayerId(): string | null {
		return this.playerId;
	}

	dispose(): void {
		this.disposed = true;
		if (this.reconnectTimer !== null) {
			window.clearTimeout(this.reconnectTimer);
			this.reconnectTimer = null;
		}
		this.ws?.close();
		this.ws = null;
	}
}
