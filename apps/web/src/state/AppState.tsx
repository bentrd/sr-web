import {
	createContext,
	useCallback,
	useContext,
	useEffect,
	useMemo,
	useRef,
	useState,
	type ReactNode,
} from "react";
import type {
	ChatMsg,
	PublicRoomSummary,
	RGB,
	RoomState,
	ServerMsg,
} from "@sr-web/protocol";
import { WsClient, type WsStatus } from "../net/ws";
import { randomColor } from "../lobby/color";
import { type Bindings, loadBindings, saveBindings } from "./bindings";
import { type Visuals, loadVisuals, saveVisuals, VISUAL_DEFAULTS } from "./visuals";
import { type SavedTrail, loadSavedTrails, saveSavedTrails } from "./savedTrails";
import { type QuickChatConfig, loadQuickChat, saveQuickChat } from "./quickChat";

const MAX_CHAT_HISTORY = 80;

const WS_URL = import.meta.env.VITE_WS_URL ?? "ws://localhost:4000/ws";
const IDENTITY_KEY = "sr-web.identity";
const FPS_KEY = "sr-web.target-fps";

// Match what the C++ side accepts (sr_set_target_fps clamps internally too).
// In the WASM build sim and render share a single tick_frame() loop, so
// this slider effectively controls the engine rate; we default to 300Hz
// to match the desktop build's limit_rate(300).
export const FPS_MIN = 30;
export const FPS_MAX = 300;
export const FPS_DEFAULT = 300;
const LEGACY_FPS_DEFAULT = 60;

function loadTargetFps(): number {
	try {
		const raw = localStorage.getItem(FPS_KEY);
		if (raw === null) return FPS_DEFAULT;
		const n = Number(raw);
		if (!Number.isFinite(n)) return FPS_DEFAULT;
		const clamped = Math.max(FPS_MIN, Math.min(FPS_MAX, Math.round(n)));
		// One-time migration: bump anyone still on the previous default
		// (60) up to the new default (300). Users who really want to
		// throttle can drag the slider back down.
		if (clamped === LEGACY_FPS_DEFAULT) return FPS_DEFAULT;
		return clamped;
	} catch {
		return FPS_DEFAULT;
	}
}

function saveTargetFps(fps: number): void {
	try {
		localStorage.setItem(FPS_KEY, String(fps));
	} catch {
		// localStorage may be disabled
	}
}

export type IdentityTrail = {
	name: string;
	b64: string;
	// Optional data: URL for the trail's per-trail thumbnail
	// (`image/png;base64,...`). Lets the pill/dropdown render the icon
	// without re-unzipping the .srt blob on every render.
	iconDataUrl?: string;
};

export type Identity = {
	name: string;
	color: RGB;
	// Optional .srt trail picked by the user. Persisted to localStorage
	// as a base64-encoded zip blob so it survives reconnects; broadcast
	// to peers via the WS `trail_share` message on game start.
	trail: IdentityTrail | null;
};

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
				let trail: Identity["trail"] = null;
				if (
					parsed.trail &&
					typeof parsed.trail === "object" &&
					typeof (parsed.trail as { name?: unknown }).name === "string" &&
					typeof (parsed.trail as { b64?: unknown }).b64 === "string"
				) {
					const t = parsed.trail as { name: string; b64: string; iconDataUrl?: unknown };
					trail = {
						name: t.name,
						b64: t.b64,
						...(typeof t.iconDataUrl === "string" ? { iconDataUrl: t.iconDataUrl } : {}),
					};
				}
				return { name: parsed.name, color: parsed.color as RGB, trail };
			}
		}
	} catch {
		// fall through
	}
	return { name: "", color: randomColor(), trail: null };
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
	targetFps: number;
	setTargetFps: (fps: number) => void;
	visuals: Visuals;
	setVisuals: (v: Visuals) => void;
	resetVisuals: () => void;
	savedTrails: readonly SavedTrail[];
	setSavedTrails: (next: readonly SavedTrail[]) => void;
	room: RoomState | null;
	leaveRoom: () => void;
	chat: readonly ChatMsg[];
	sendChat: (text: string) => void;
	lastError: { code: string; message: string } | null;
	clearError: () => void;
	publicRooms: readonly PublicRoomSummary[];
	subscribePublicRooms: () => void;
	unsubscribePublicRooms: () => void;
	quickChat: QuickChatConfig;
	setQuickChat: (q: QuickChatConfig) => void;
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
	const [targetFps, setTargetFpsState] = useState<number>(() => loadTargetFps());
	const [visuals, setVisualsState] = useState<Visuals>(() => loadVisuals());
	const [savedTrails, setSavedTrailsState] =
		useState<readonly SavedTrail[]>(() => loadSavedTrails());
	const [room, setRoom] = useState<RoomState | null>(null);
	const [chat, setChat] = useState<readonly ChatMsg[]>([]);
	const [lastError, setLastError] =
		useState<{ code: string; message: string } | null>(null);
	const [publicRooms, setPublicRooms] = useState<readonly PublicRoomSummary[]>([]);
	const [quickChat, setQuickChatState] = useState<QuickChatConfig>(() => loadQuickChat());

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
				case "public_rooms_list":
					setPublicRooms(msg.rooms);
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

	const setTargetFps = (fps: number): void => {
		const clamped = Math.max(FPS_MIN, Math.min(FPS_MAX, Math.round(fps)));
		setTargetFpsState(clamped);
		saveTargetFps(clamped);
	};

	const setVisuals = (v: Visuals): void => {
		setVisualsState(v);
		saveVisuals(v);
	};

	const resetVisuals = (): void => {
		setVisualsState(VISUAL_DEFAULTS);
		saveVisuals(VISUAL_DEFAULTS);
	};

	const setSavedTrails = (next: readonly SavedTrail[]): void => {
		setSavedTrailsState(next);
		saveSavedTrails(next);
	};

	const setQuickChat = (next: QuickChatConfig): void => {
		setQuickChatState(next);
		saveQuickChat(next);
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

	const subscribePublicRooms = useCallback((): void => {
		ws.send({ type: "subscribe_public_rooms" });
	}, [ws]);

	const unsubscribePublicRooms = useCallback((): void => {
		ws.send({ type: "unsubscribe_public_rooms" });
	}, [ws]);

	const value = useMemo<AppState>(
		() => ({
			ws,
			wsStatus,
			playerId,
			identity,
			setIdentity,
			bindings,
			setBindings,
			targetFps,
			setTargetFps,
			visuals,
			setVisuals,
			resetVisuals,
			savedTrails,
			setSavedTrails,
			room,
			leaveRoom,
			chat,
			sendChat,
			lastError,
			clearError: () => setLastError(null),
			publicRooms,
			subscribePublicRooms,
			unsubscribePublicRooms,
			quickChat,
			setQuickChat,
		}),
		// eslint-disable-next-line react-hooks/exhaustive-deps
		[ws, wsStatus, playerId, identity, bindings, targetFps, visuals, savedTrails, room, chat, lastError, publicRooms, quickChat],
	);

	return <AppCtx.Provider value={value}>{children}</AppCtx.Provider>;
}

export function useApp(): AppState {
	const v = useContext(AppCtx);
	if (!v) throw new Error("useApp must be used inside <AppProvider>");
	return v;
}

