import {
	createContext,
	useContext,
	useEffect,
	useMemo,
	useRef,
	useState,
	type ReactNode,
} from "react";
import type { RGB, RoomState, ServerMsg } from "@sr-web/protocol";
import { WsClient, type WsStatus } from "../net/ws";

const WS_URL = import.meta.env.VITE_WS_URL ?? "ws://localhost:4000/ws";
const IDENTITY_KEY = "sr-web.identity";

export type Identity = { name: string; color: RGB };

const DEFAULT_IDENTITY: Identity = {
	name: "",
	color: [0.95, 0.4, 0.4],
};

function loadIdentity(): Identity {
	try {
		const raw = localStorage.getItem(IDENTITY_KEY);
		if (!raw) return DEFAULT_IDENTITY;
		const parsed = JSON.parse(raw) as Partial<Identity>;
		if (
			typeof parsed.name === "string" &&
			Array.isArray(parsed.color) &&
			parsed.color.length === 3
		) {
			return { name: parsed.name, color: parsed.color as RGB };
		}
	} catch {
		// fall through
	}
	return DEFAULT_IDENTITY;
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
	room: RoomState | null;
	leaveRoom: () => void;
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
	const [room, setRoom] = useState<RoomState | null>(null);
	const [lastError, setLastError] =
		useState<{ code: string; message: string } | null>(null);

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

	const leaveRoom = (): void => {
		ws.send({ type: "leave_room" });
		setRoom(null);
		setLastError(null);
	};

	const value = useMemo<AppState>(
		() => ({
			ws,
			wsStatus,
			playerId,
			identity,
			setIdentity,
			room,
			leaveRoom,
			lastError,
			clearError: () => setLastError(null),
		}),
		// eslint-disable-next-line react-hooks/exhaustive-deps
		[ws, wsStatus, playerId, identity, room, lastError],
	);

	return <AppCtx.Provider value={value}>{children}</AppCtx.Provider>;
}

export function useApp(): AppState {
	const v = useContext(AppCtx);
	if (!v) throw new Error("useApp must be used inside <AppProvider>");
	return v;
}

