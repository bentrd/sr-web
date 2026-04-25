import { useEffect, useState, type FormEvent } from "react";
import { useNavigate } from "react-router-dom";
import { useApp } from "../state/AppState";
import { hexToRgb, rgbToHex } from "./color";
import { MAPS } from "./maps";
import { ControlsModal } from "./ControlsModal";

export function Home(): JSX.Element {
	const { identity, setIdentity, ws, wsStatus, lastError, clearError, room } =
		useApp();
	const navigate = useNavigate();

	const [mapId, setMapId] = useState<string>(MAPS[0]?.id ?? "");
	const [joinCode, setJoinCode] = useState<string>("");
	const [controlsOpen, setControlsOpen] = useState(false);

	const canSubmit = identity.name.trim().length > 0 && wsStatus.kind === "open";

	function handleCreate(e: FormEvent): void {
		e.preventDefault();
		if (!canSubmit || !mapId) return;
		clearError();
		ws.send({
			type: "create_room",
			name: identity.name.trim(),
			color: identity.color,
			mapId,
		});
		// Navigation is driven by the effect below once `room_state` lands.
	}

	function handleJoin(e: FormEvent): void {
		e.preventDefault();
		if (!canSubmit || !joinCode.trim()) return;
		clearError();
		const code = joinCode.trim().toUpperCase();
		ws.send({
			type: "join_room",
			code,
			name: identity.name.trim(),
			color: identity.color,
		});
		navigate(`/r/${code}`);
	}

	// When `room_state` arrives (after Create), navigate to the room URL.
	useEffect(() => {
		if (room) navigate(`/r/${room.code}`, { replace: true });
	}, [room, navigate]);

	return (
		<main className="lobby">
			<h1>SR-Web</h1>
			<p className="subtitle">Browser SpeedRunners with ghost multiplayer.</p>

			<section className="card">
				<h2>You</h2>
				<div className="field-row">
					<label className="field">
						<span>Name</span>
						<input
							type="text"
							maxLength={24}
							value={identity.name}
							onChange={(e) =>
								setIdentity({ ...identity, name: e.target.value })
							}
							placeholder="anonymous_runner"
							autoFocus
						/>
					</label>
					<label className="field field-color">
						<span>Color</span>
						<input
							type="color"
							value={rgbToHex(identity.color)}
							onChange={(e) =>
								setIdentity({ ...identity, color: hexToRgb(e.target.value) })
							}
						/>
					</label>
				</div>
			</section>

			<div className="two-col">
				<form className="card" onSubmit={handleCreate}>
					<h2>Create room</h2>
					<label className="field">
						<span>Map</span>
						<select value={mapId} onChange={(e) => setMapId(e.target.value)}>
							{MAPS.map((m) => (
								<option key={m.id} value={m.id}>
									{m.displayName}
								</option>
							))}
						</select>
					</label>
					<button type="submit" disabled={!canSubmit}>
						Create
					</button>
				</form>

				<form className="card" onSubmit={handleJoin}>
					<h2>Join room</h2>
					<label className="field">
						<span>Code</span>
						<input
							type="text"
							value={joinCode}
							onChange={(e) => setJoinCode(e.target.value)}
							placeholder="e.g. 7K2QM"
							maxLength={8}
							autoCapitalize="characters"
							spellCheck={false}
							style={{ textTransform: "uppercase", letterSpacing: "0.1em" }}
						/>
					</label>
					<button type="submit" disabled={!canSubmit || !joinCode.trim()}>
						Join
					</button>
				</form>
			</div>

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
				server: {wsStatus.kind}
				{wsStatus.kind === "closed" && ` — ${wsStatus.reason}`}
			</footer>
			<ControlsModal open={controlsOpen} onClose={() => setControlsOpen(false)} />
		</main>
	);
}
