import {
	createContext,
	useContext,
	useEffect,
	useMemo,
	useRef,
	useState,
	type ReactNode,
} from "react";
import type { ChatMsg, RGB, RoomState, ServerMsg } from "@sr-web/protocol";
import { WsClient, type WsStatus } from "../net/ws";
import { randomColor } from "../lobby/color";
import { type Bindings, loadBindings, saveBindings } from "./bindings";

const MAX_CHAT_HISTORY = 80;

const WS_URL = import.meta.env.VITE_WS_URL ?? "ws://localhost:4000/ws";
const IDENTITY_KEY = "sr-web.identity";

export type Identity = { name: string; color: RGB };

function loadIdentity(): Identity {
	try {
		const raw = localStorage.getItem(IDENTITY_KEY);
		if (raw) {
			const parsed = JSON.parse(raw) as Partial<Identity>;
			if (
				typeof parsed.name === "string" &&
				Array.isArray(parsed.color) &&
				parsed.color.length === 3
			) {
				return { name: parsed.name, color: parsed.color as RGB };
			}
		}
	} catch {
		// fall through
	}
	return { name: "", color: randomColor() };
}

function saveIdentity(i: Identity): void {
	try {
		localStorage.setItem(IDENTITY_KEY, JSON.stringify(i));
	} catch {
		// localStorage may be disabled (private mode); not fatal
	}
}

export type AppState = {
	ws: WsClient;
	wsStatus: WsStatus;
	playerId: string | null;
	identity: Identity;
	setIdentity: (i: Identity) => void;
	bindings: Bindings;
	setBindings: (b: Bindings) => void;
	room: RoomState | null;
	leaveRoom: () => void;
	chat: readonly ChatMsg[];
	sendChat: (text: string) => void;
	lastError: { code: string; message: string } | null;
	clearError: () => void;
};

const AppCtx = createContext<AppState | null>(null);

export function AppProvider({ children }: { children: ReactNode }): JSX.Element {
	const wsRef = useRef<WsClient | null>(null);
	if (wsRef.current === null) wsRef.current = new WsClient(WS_URL);
	const ws = wsRef.current;

	const [wsStatus, setWsStatus] = useState<WsStatus>(ws.getStatus());
	const [playerId, setPlayerId] = useState<string | null>(ws.getPlayerId());
	const [identity, setIdentityState] = useState<Identity>(() => loadIdentity());
	const [bindings, setBindingsState] = useState<Bindings>(() => loadBindings());
	const [room, setRoom] = useState<RoomState | null>(null);
	const [chat, setChat] = useState<readonly ChatMsg[]>([]);
	const [lastError, setLastError] =
		useState<{ code: string; message: string } | null>(null);

	// Block key events from reaching Emscripten's GLFW shim while a text
	// input has focus. Emscripten registers `addEventListener("keydown",
	// GLFW.onKeydown, true)` on window in capture phase; we install our
	// own capture-phase listener BEFORE WASM loads, so it fires first and
	// we can stopImmediatePropagation() to keep chat keystrokes out of the
	// game. Same treatment for keyup so held-key state stays consistent.
	useEffect(() => {
		const isTypingTarget = (el: EventTarget | null): boolean => {
			if (!(el instanceof HTMLElement)) return false;
			if (el.isContentEditable) return true;
			const tag = el.tagName;
			return tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT";
		};
		const onKey = (e: KeyboardEvent): void => {
			if (isTypingTarget(document.activeElement)) {
				e.stopImmediatePropagation();
			}
		};
		window.addEventListener("keydown", onKey, true);
		window.addEventListener("keyup", onKey, true);
		return () => {
			window.removeEventListener("keydown", onKey, true);
			window.removeEventListener("keyup", onKey, true);
		};
	}, []);

	useEffect(() => {
		const offStatus = ws.onStatus(setWsStatus);
		const offMsg = ws.onMessage((msg: ServerMsg) => {
			switch (msg.type) {
				case "welcome":
					setPlayerId(msg.playerId);
					break;
				case "room_state":
					setRoom(msg);
					break;
				case "player_left":
					// No state change — `room_state` follows on the server side.
					break;
				case "game_started":
					// Will trigger navigation in Room.tsx via the `started` flag in room_state
					break;
				case "chat":
					setChat((prev) => {
						const next = [...prev, msg];
						return next.length > MAX_CHAT_HISTORY
							? next.slice(next.length - MAX_CHAT_HISTORY)
							: next;
					});
					break;
				case "error":
					setLastError({ code: msg.code, message: msg.message });
					break;
			}
		});
		return () => {
			offStatus();
			offMsg();
		};
	}, [ws]);

	// Note: we deliberately don't dispose the WsClient in a cleanup effect.
	// In StrictMode (dev), effects run mount→unmount→mount which would
	// permanently dispose the singleton and break reconnection. The WS
	// closes naturally when the browser tab does.

	const setIdentity = (i: Identity): void => {
		setIdentityState(i);
		saveIdentity(i);
	};

	const setBindings = (b: Bindings): void => {
		setBindingsState(b);
		saveBindings(b);
	};

	const leaveRoom = (): void => {
		ws.send({ type: "leave_room" });
		setRoom(null);
		setChat([]);
		setLastError(null);
	};

	const sendChat = (text: string): void => {
		const trimmed = text.trim();
		if (!trimmed) return;
		ws.send({ type: "chat_send", text: trimmed.slice(0, 240) });
	};

	const value = useMemo<AppState>(
		() => ({
			ws,
			wsStatus,
			playerId,
			identity,
			setIdentity,
			bindings,
			setBindings,
			room,
			leaveRoom,
			chat,
			sendChat,
			lastError,
			clearError: () => setLastError(null),
		}),
		// eslint-disable-next-line react-hooks/exhaustive-deps
		[ws, wsStatus, playerId, identity, bindings, room, chat, lastError],
	);

	return <AppCtx.Provider value={value}>{children}</AppCtx.Provider>;
}

export function useApp(): AppState {
	const v = useContext(AppCtx);
	if (!v) throw new Error("useApp must be used inside <AppProvider>");
	return v;
}

