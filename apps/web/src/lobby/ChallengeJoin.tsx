import { useEffect, useState, type FormEvent } from "react";
import { useNavigate } from "react-router-dom";
import { useApp } from "../state/AppState";
import { hexToRgb, rgbToHex } from "./color";

export function ChallengeJoin({ code }: { code: string }): JSX.Element {
	const { identity, setIdentity, ws, wsStatus, room } = useApp();
	const navigate = useNavigate();
	const [name, setName] = useState<string>(identity.name);

	// If we're already in the target room, navigate to it.
	useEffect(() => {
		if (room && room.code === code) {
			navigate(`/r/${code}`, { replace: true });
		}
	}, [room, code, navigate]);

	const noName = name.trim().length === 0;
	const canJoin = !noName && wsStatus.kind === "open";

	function handleJoin(e: FormEvent): void {
		e.preventDefault();
		if (!canJoin) return;
		const trimmed = name.trim();
		setIdentity({ ...identity, name: trimmed });
		ws.send({
			type: "join_room",
			code,
			name: trimmed,
			color: identity.color,
		});
	}

	return (
		<main className="flex min-h-screen items-center justify-center bg-zinc-950">
			<form
				onSubmit={handleJoin}
				className="flex w-full max-w-sm flex-col gap-4 rounded-xl border border-zinc-800 bg-zinc-900 p-6"
			>
				<h1 className="text-lg font-semibold text-zinc-100">
					{code === "SPEED"
						? "Speed Challenge"
						: code === "RGCH1"
							? "RG Challenge"
							: code === "RACE1"
								? "Time Challenge"
								: "Challenge"}
				</h1>
				<p className="text-sm text-zinc-400">
					Enter your name to join the public challenge room.
				</p>
				<label className="flex flex-col gap-1.5">
					<span className="text-xs font-medium text-zinc-400">Name</span>
					<input
						type="text"
						maxLength={24}
						value={name}
						onChange={(e) => setName(e.target.value)}
						placeholder="Your name"
						autoFocus
						className="h-10 rounded-lg border border-zinc-800 bg-zinc-950 px-3 text-sm text-zinc-100 outline-none placeholder:text-zinc-500 focus:border-zinc-600"
					/>
				</label>
				<label className="flex flex-col gap-1.5">
					<span className="text-xs font-medium text-zinc-400">Color</span>
					<input
						type="color"
						value={rgbToHex(identity.color)}
						onChange={(e) => {
							setIdentity({ ...identity, color: hexToRgb(e.target.value) });
						}}
						className="h-10 w-full cursor-pointer rounded-lg border border-zinc-800 bg-zinc-950 p-1"
					/>
				</label>
				<button
					type="submit"
					disabled={!canJoin}
					className="h-10 rounded-lg bg-emerald-600 px-4 text-sm font-semibold text-white transition hover:bg-emerald-500 disabled:opacity-40"
				>
					{wsStatus.kind === "connecting" ? "Connecting…" : "Join Room"}
				</button>
			</form>
		</main>
	);
}