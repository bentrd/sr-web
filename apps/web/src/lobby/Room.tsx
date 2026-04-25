import { useEffect, useRef } from "react";
import { useNavigate, useParams } from "react-router-dom";
import { useApp } from "../state/AppState";
import { rgbToCss } from "./color";
import { MAPS } from "./maps";

const SOFT_CAP_WARNING = 12;

export function Room(): JSX.Element {
	const { code: urlCode } = useParams<{ code: string }>();
	const navigate = useNavigate();
	const { ws, wsStatus, playerId, identity, room, lastError, leaveRoom } =
		useApp();

	// If we land directly on /r/CODE without an active room state for that
	// code (e.g. shared link), auto-join — provided we have an identity.
	const joinAttemptedRef = useRef(false);
	useEffect(() => {
		if (!urlCode) return;
		if (wsStatus.kind !== "open") return;
		if (room?.code === urlCode.toUpperCase()) return;
		if (!identity.name.trim()) {
			// Need a name first — bounce to home.
			navigate("/", { replace: true });
			return;
		}
		if (joinAttemptedRef.current) return;
		joinAttemptedRef.current = true;
		ws.send({
			type: "join_room",
			code: urlCode.toUpperCase(),
			name: identity.name.trim(),
			color: identity.color,
		});
	}, [urlCode, wsStatus.kind, room?.code, identity, ws, navigate]);

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

	const isHost = room.hostId === playerId;
	const map = MAPS.find((m) => m.id === room.mapId);
	const overCap = room.players.length > SOFT_CAP_WARNING;

	function handleLeave(): void {
		leaveRoom();
		navigate("/");
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
					<div className="room-code-label">Room code</div>
					<div className="room-code">{room.code}</div>
				</div>
				<div className="room-map">
					<div className="room-code-label">Map</div>
					<div>{map?.displayName ?? room.mapId}</div>
				</div>
				<button type="button" onClick={handleLeave}>
					Leave
				</button>
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
				server: {wsStatus.kind}
				{wsStatus.kind === "closed" && ` — ${wsStatus.reason}`}
			</footer>
		</main>
	);
}
