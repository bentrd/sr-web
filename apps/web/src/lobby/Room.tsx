import { useEffect, useRef, useState, type FormEvent } from "react";
import { useNavigate, useParams } from "react-router-dom";
import { useApp } from "../state/AppState";
import { Game } from "../game/Game";
import { hexToRgb, rgbToCss, rgbToHex } from "./color";
import { MAPS } from "./maps";
import { ControlsModal } from "./ControlsModal";
import { OptionsModal } from "./OptionsModal";
import { ChatPanel } from "./ChatPanel";
import { QuickChatModal } from "./QuickChatModal";

const SOFT_CAP_WARNING = 12;

export function Room(): JSX.Element {
	const { code: urlCode } = useParams<{ code: string }>();
	const navigate = useNavigate();
	const { ws, wsStatus, playerId, identity, setIdentity, room, lastError, leaveRoom } =
		useApp();
	const [controlsOpen, setControlsOpen] = useState(false);
	const [optionsOpen, setOptionsOpen] = useState(false);
	const [quickChatOpen, setQuickChatOpen] = useState(false);
	const [copyState, setCopyState] = useState<"idle" | "copied">("idle");

	// If we land directly on /r/CODE without an active room state for that
	// code (e.g. shared link, page refresh, WS reconnect), auto-join.
	// We re-issue join_room on each fresh "open" so a refresh inside an
	// already-started room rejoins via our persisted player id.
	const lastJoinedRef = useRef<{ code: string; openAt: number } | null>(null);
	useEffect(() => {
		if (!urlCode) return;
		if (wsStatus.kind !== "open") {
			// Reset so the next open re-attempts.
			lastJoinedRef.current = null;
			return;
		}
		const code = urlCode.toUpperCase();
		if (room?.code === code) return;
		if (!identity.name.trim()) {
			// Don't bounce to "/" — render an inline join form below so the
			// shared URL stays stable and the new player goes straight in
			// after entering a name.
			return;
		}
		// Avoid spamming join_room within the same WS session.
		if (lastJoinedRef.current?.code === code) return;
		lastJoinedRef.current = { code, openAt: Date.now() };
		ws.send({
			type: "join_room",
			code,
			name: identity.name.trim(),
			color: identity.color,
		});
	}, [urlCode, wsStatus.kind, room?.code, identity, ws, navigate]);

	// Visitor opened a shared invite link without a stored identity — collect
	// just the name (color defaults to a random saturated hue, can be tweaked).
	if (urlCode && !identity.name.trim()) {
		return <JoinViaLink code={urlCode.toUpperCase()} />;
	}

	if (!room || (urlCode && room.code !== urlCode.toUpperCase())) {
		return (
			<main className="lobby">
				<h1>Joining…</h1>
				{lastError && <div className="error">{lastError.message}</div>}
				<button type="button" onClick={() => navigate("/")}>
					Back
				</button>
			</main>
		);
	}

	async function handleCopyLink(): Promise<void> {
		// HashRouter encodes routes after `#`, and the app may be served
		// from a sub-path (e.g. /sr-web/) on GitHub Pages. Build the URL
		// from the current document so both work.
		const { origin, pathname } = window.location;
		const base = pathname.split("#")[0]?.replace(/index\.html$/, "") ?? "/";
		const url = `${origin}${base}#/r/${room?.code ?? ""}`;
		try {
			await navigator.clipboard.writeText(url);
			setCopyState("copied");
			window.setTimeout(() => setCopyState("idle"), 1500);
		} catch {
			// Clipboard can fail in insecure contexts — silently no-op.
		}
	}

	// Once the host starts, swap to the game view. The lobby chrome
	// (player list, leave button) collapses into a thin top bar so the
	// canvas owns the screen.
	if (room.started) {
		return (
			<main className="game-page">
				<header className="game-bar">
					<span className="game-bar-room">{room.code}</span>
					<span className="game-bar-players">{room.players.length} players</span>
					<button type="button" onClick={() => setControlsOpen(true)}>
						Controls
					</button>
					<button type="button" onClick={() => setOptionsOpen(true)}>
						Options
					</button>
					<button type="button" onClick={() => setQuickChatOpen(true)}>
						Quick Chat
					</button>
					<button type="button" onClick={() => { leaveRoom(); navigate("/"); }}>
						Leave
					</button>
				</header>
				<Game />
				<ChatPanel variant="game" />
				<ControlsModal open={controlsOpen} onClose={() => setControlsOpen(false)} />
				<OptionsModal open={optionsOpen} onClose={() => setOptionsOpen(false)} />
				<QuickChatModal open={quickChatOpen} onClose={() => setQuickChatOpen(false)} />
			</main>
		);
	}

	const isHost = room.hostId === playerId;
	const map = MAPS.find((m) => m.id === room.mapId);
	const overCap = room.players.length > SOFT_CAP_WARNING;

	function handleLeave(): void {
		leaveRoom();
		navigate("/");
	}

	function handleToggleVisibility(): void {
		if (!room) return;
		ws.send({ type: "set_room_visibility", public: !room.public });
	}

	function handleStart(): void {
		ws.send({ type: "start_game" });
		// When `room_state.started` flips true, the App router will
		// (in a future phase) swap to the <Game /> component.
	}

	return (
		<main className="lobby">
			<header className="room-header">
				<div>
					<div className="room-code-label">Lobby</div>
					<div className="room-display-name">{room.displayName}</div>
					<div className="room-code-sub">
						code <span className="room-code-inline">{room.code}</span>
					</div>
				</div>
				<div className="room-map">
					<div className="room-code-label">Map</div>
					<div>
						{room.mode === "grapple_challenge"
							? "Speed Challenge"
							: room.mode === "rg_challenge"
								? "RG Challenge"
								: room.mode === "time_challenge"
									? "Time Challenge"
									: (map?.displayName ?? room.mapId)}
					</div>
				</div>
				<div className="room-map">
					<div className="room-code-label">Capacity</div>
					<div>
						{room.players.length}/
						{room.maxPlayers === -1 ? "∞" : room.maxPlayers}
					</div>
				</div>
				<div className="room-actions">
					{isHost && (
						<button
							type="button"
							className={room.public ? "visibility-public" : "visibility-private"}
							onClick={handleToggleVisibility}
						>
							{room.public ? "Public · click to make private" : "Private · click to make public"}
						</button>
					)}
					<button type="button" onClick={handleCopyLink}>
						{copyState === "copied" ? "Copied!" : "Copy invite link"}
					</button>
					<button type="button" onClick={handleLeave}>
						Leave
					</button>
				</div>
			</header>

			<section className="card">
				<h2>Players ({room.players.length})</h2>
				<ul className="player-list">
					{room.players.map((p) => (
						<li key={p.id}>
							<span
								className="swatch"
								style={{ background: rgbToCss(p.color) }}
								aria-hidden
							/>
							<span className="player-name">{p.name}</span>
							{p.id === room.hostId && <span className="host-tag">host</span>}
							{p.id === playerId && <span className="you-tag">you</span>}
						</li>
					))}
				</ul>
				{overCap && (
					<p className="warn">
						⚠ {room.players.length} players in the room. Performance and
						bandwidth get rough past ~12.
					</p>
				)}
			</section>

			{isHost && (
				<button
					type="button"
					className="primary"
					onClick={handleStart}
					disabled={room.started}
				>
					{room.started ? "Game started" : "Start game"}
				</button>
			)}
			{!isHost && (
				<p className="hint">Waiting for the host to start the game…</p>
			)}

			{lastError && (
				<div className="error" role="alert">
					{lastError.message}
				</div>
			)}

			<footer className="status">
				<button
					type="button"
					className="link-button"
					onClick={() => setControlsOpen(true)}
				>
					Controls
				</button>
				<span className="status-dot">·</span>
				<button
					type="button"
					className="link-button"
					onClick={() => setOptionsOpen(true)}
				>
					Options
				</button>
				<span className="status-dot">·</span>
				<button
					type="button"
					className="link-button"
					onClick={() => setQuickChatOpen(true)}
				>
					Quick Chat
				</button>
				<span className="status-dot">·</span>
				server: {wsStatus.kind}
				{wsStatus.kind === "closed" && ` — ${wsStatus.reason}`}
			</footer>
			<ChatPanel variant="lobby" />
			<ControlsModal open={controlsOpen} onClose={() => setControlsOpen(false)} />
			<OptionsModal open={optionsOpen} onClose={() => setOptionsOpen(false)} />
			<QuickChatModal open={quickChatOpen} onClose={() => setQuickChatOpen(false)} />
		</main>
	);
}

// Inline name+color form rendered when a visitor opens a shared invite link
// (/r/CODE) without a stored identity. Submitting commits the identity to
// AppState, which re-runs Room's join effect and slots them straight into the
// room — no detour through the lobby home.
function JoinViaLink({ code }: { code: string }): JSX.Element {
	const { identity, setIdentity, wsStatus, lastError } = useApp();
	const [name, setName] = useState<string>("");

	function handleSubmit(e: FormEvent): void {
		e.preventDefault();
		const trimmed = name.trim();
		if (!trimmed || wsStatus.kind !== "open") return;
		setIdentity({ ...identity, name: trimmed });
	}

	return (
		<main className="lobby">
			<h1>Join room {code}</h1>
			<p className="subtitle">Pick a name and color, then jump in.</p>
			<form className="card" onSubmit={handleSubmit}>
				<div className="field-row">
					<label className="field">
						<span>Name</span>
						<input
							type="text"
							maxLength={24}
							value={name}
							onChange={(e) => setName(e.target.value)}
							placeholder="anonymous_runner"
							autoFocus
						/>
					</label>
					<label className="field field-color">
						<span>Color</span>
						<input
							type="color"
							value={rgbToHex(identity.color)}
							onChange={(e) => setIdentity({ ...identity, color: hexToRgb(e.target.value) })}
						/>
					</label>
				</div>
				<button
					type="submit"
					className="primary"
					disabled={!name.trim() || wsStatus.kind !== "open"}
				>
					Join
				</button>
			</form>
			{lastError && (
				<div className="error" role="alert">
					{lastError.message}
				</div>
			)}
			<footer className="status">
				server: {wsStatus.kind}
				{wsStatus.kind === "closed" && ` — ${wsStatus.reason}`}
			</footer>
		</main>
	);
}
