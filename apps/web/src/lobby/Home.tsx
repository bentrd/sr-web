import { useEffect, useMemo, useState, type FormEvent } from "react";
import { useNavigate } from "react-router-dom";
import { useApp } from "../state/AppState";
import { hexToRgb, rgbToCss, rgbToHex } from "./color";
import { MAPS } from "./maps";
import { ControlsModal } from "./ControlsModal";
import { TrailMenu } from "./TrailMenu";
import type { GameMode } from "@sr-web/protocol";

export function Home(): JSX.Element {
	const {
		identity,
		setIdentity,
		ws,
		wsStatus,
		lastError,
		clearError,
		room,
		publicRooms,
		subscribePublicRooms,
		unsubscribePublicRooms,
	} = useApp();
	const navigate = useNavigate();

	const [mode, setMode] = useState<GameMode>("standard");
	const [mapId, setMapId] = useState<string>(MAPS[0]?.id ?? "");
	const [joinCode, setJoinCode] = useState<string>("");
	const [controlsOpen, setControlsOpen] = useState(false);
	const [displayName, setDisplayName] = useState<string>("");
	const [maxPlayers, setMaxPlayers] = useState<number>(-1);
	const [isPublic, setIsPublic] = useState<boolean>(false);
	const [filter, setFilter] = useState<string>("");

	const effectiveDisplayName =
		displayName.trim().length > 0
			? displayName.trim()
			: `${identity.name.trim() || "anonymous"}'s lobby`;

	const noName = identity.name.trim().length === 0;
	const canSubmit = !noName && wsStatus.kind === "open";
	// Single source of truth for why a submit button is disabled, fed to the
	// `title` attribute so the user sees the reason on hover instead of
	// guessing why nothing happens.
	const disabledReason = noName
		? "Enter a name to play"
		: wsStatus.kind !== "open"
			? "Connecting to server…"
			: undefined;

	const filteredPublic = useMemo(() => {
		const q = filter.trim().toLowerCase();
		if (!q) return publicRooms;
		return publicRooms.filter((r) => {
			const map = MAPS.find((m) => m.id === r.mapId);
			return (
				r.displayName.toLowerCase().includes(q) ||
				r.code.toLowerCase().includes(q) ||
				(map?.displayName.toLowerCase() ?? r.mapId.toLowerCase()).includes(q)
			);
		});
	}, [publicRooms, filter]);

	function handleCreate(e: FormEvent): void {
		e.preventDefault();
		if (!canSubmit) return;
		if (mode === "standard" && !mapId) return;
		clearError();
		const actualMapId = mode === "grapple_challenge" ? "grapple_challenge" : mapId;
		ws.send({
			type: "create_room",
			name: identity.name.trim(),
			color: identity.color,
			mapId: actualMapId,
			mode,
			displayName: effectiveDisplayName.slice(0, 48),
			maxPlayers,
			public: isPublic,
		});
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

	function handleJoinPublic(code: string): void {
		if (!canSubmit) return;
		clearError();
		ws.send({
			type: "join_room",
			code,
			name: identity.name.trim(),
			color: identity.color,
		});
		navigate(`/r/${code}`);
	}

	useEffect(() => {
		if (room) navigate(`/r/${room.code}`, { replace: true });
	}, [room, navigate]);

	useEffect(() => {
		if (wsStatus.kind !== "open") return;
		subscribePublicRooms();
		return () => unsubscribePublicRooms();
	}, [wsStatus.kind, subscribePublicRooms, unsubscribePublicRooms]);

	const statusDot =
		wsStatus.kind === "open"
			? "bg-emerald-400/80"
			: wsStatus.kind === "connecting"
				? "bg-amber-400/80"
				: "bg-red-400/80";

	return (
		<main className="mx-auto flex w-full max-w-[128rem] flex-col gap-6 px-6 py-5">
			<header className="flex flex-wrap items-center justify-between gap-4">
				<div className="flex h-12 items-center gap-3">
					<div className="size-12 rounded-xl bg-gradient-to-br from-amber-600/80 to-rose-700/80" />
					<div className="flex flex-col justify-center">
						<div className="text-xl font-semibold leading-tight tracking-tight text-zinc-100">SR-Web</div>
						<div className="text-xs leading-tight text-zinc-500">Browser SpeedRunners · ghost MP</div>
					</div>
				</div>

				<div
					className={`relative flex h-12 items-center gap-1 rounded-xl border bg-zinc-900/60 p-1 pl-1 transition ${
						noName
							? "border-amber-400/60 ring-2 ring-amber-400/30 sr-pulse"
							: "border-zinc-800"
					}`}
				>
					{noName && (
						<span
							className="pointer-events-none absolute -bottom-7 right-2 whitespace-nowrap text-xs font-medium text-amber-300/90"
							aria-hidden
						>
							↑ pick a name to start
						</span>
					)}
					<label className="relative h-9 w-9 shrink-0 cursor-pointer overflow-hidden rounded-lg border border-zinc-700">
						<span
							className="absolute inset-0"
							style={{ background: rgbToCss(identity.color) }}
							aria-hidden
						/>
						<input
							type="color"
							value={rgbToHex(identity.color)}
							onChange={(e) =>
								setIdentity({ ...identity, color: hexToRgb(e.target.value) })
							}
							className="absolute inset-0 cursor-pointer opacity-0"
						/>
					</label>
					<input
						type="text"
						maxLength={24}
						value={identity.name}
						onChange={(e) => setIdentity({ ...identity, name: e.target.value })}
						placeholder="your name"
						autoFocus={!identity.name}
						className={`h-9 w-36 bg-transparent px-2 text-sm font-medium text-zinc-100 outline-none placeholder:text-zinc-500 ${
							noName ? "placeholder:text-amber-300/70" : ""
						}`}
					/>
					<TrailMenu />
					<button
						type="button"
						onClick={() => setControlsOpen(true)}
						className="flex h-9 items-center rounded-lg border-0 bg-transparent px-3 text-xs font-medium text-zinc-300 hover:bg-zinc-800 hover:text-zinc-100"
					>
						Controls
					</button>
					<span className="ml-0.5 flex h-9 items-center gap-1.5 rounded-lg border border-zinc-700 bg-zinc-950 px-2.5 text-xs text-zinc-400">
						<span className={`size-1.5 rounded-full ${statusDot}`} />
						{wsStatus.kind}
					</span>
				</div>
			</header>

			{lastError && (
				<div className="rounded-xl border border-red-500/40 bg-red-500/10 px-4 py-2.5 text-sm text-red-300">
					{lastError.message}
				</div>
			)}

			<div
				className={`grid grid-cols-1 gap-5 transition lg:grid-cols-5 ${
					noName ? "pointer-events-none opacity-40 blur-[1px] select-none" : ""
				}`}
				aria-hidden={noName}
			>
				<section className="rounded-2xl border border-zinc-800 bg-zinc-900/60 p-5 lg:col-span-3">
					<div className="mb-4 flex flex-wrap items-end justify-between gap-3">
						<div>
							<h2 className="text-base font-medium text-zinc-200">Public lobbies</h2>
							<div className="text-xs text-zinc-400">
								{publicRooms.length === 0
									? "Nobody hosting right now."
									: `${publicRooms.length} live ${publicRooms.length === 1 ? "room" : "rooms"}`}
							</div>
						</div>
						<input
							type="text"
							value={filter}
							onChange={(e) => setFilter(e.target.value)}
							placeholder="Filter by name, code, or map…"
							className="h-10 w-full max-w-sm rounded-lg border border-zinc-800 bg-zinc-950 px-3 text-sm text-zinc-100 outline-none placeholder:text-zinc-500 focus:border-zinc-600"
						/>
					</div>

					{filteredPublic.length === 0 ? (
						<div className="flex flex-col items-center justify-center gap-2 rounded-xl border border-dashed border-zinc-800 px-6 py-14 text-center">
							<div className="text-4xl text-zinc-700" aria-hidden>◎</div>
							<div className="max-w-sm text-sm text-zinc-400">
								{publicRooms.length === 0
									? "No public lobbies yet — host one and your friends can browse in."
									: "No matches for that filter."}
							</div>
						</div>
					) : (
						<ul className="flex list-none flex-col gap-2 p-0">
							{filteredPublic.map((r) => {
								const map = MAPS.find((m) => m.id === r.mapId);
								const isChallenge = r.mode === "grapple_challenge";
								const unlimited = r.maxPlayers === -1;
								const full = !unlimited && r.playerCount >= r.maxPlayers;
								const fillPct = unlimited
									? Math.min(100, r.playerCount * 12)
									: Math.min(100, (r.playerCount / r.maxPlayers) * 100);
								const fillColor =
									fillPct > 85
										? "bg-red-500/60"
										: fillPct > 60
											? "bg-amber-500/60"
											: "bg-zinc-500";
								return (
									<li
										key={r.code}
										className="flex items-center gap-4 rounded-xl border border-zinc-800 bg-zinc-950/60 px-4 py-3 transition hover:border-zinc-700"
									>
										<div className="min-w-0 flex-1">
											<div className="truncate text-sm font-semibold text-zinc-100">
												{r.displayName}
											</div>
											<div className="mt-1 flex flex-wrap items-center gap-1.5">
												{isChallenge ? (
													<span className="rounded-md border border-emerald-500/30 bg-emerald-500/10 px-1.5 py-0.5 text-[11px] text-emerald-300">
														Grapple Challenge
													</span>
												) : (
													<span className="rounded-md border border-zinc-800 bg-zinc-900 px-1.5 py-0.5 text-[11px] text-zinc-400">
														{map?.displayName ?? r.mapId}
													</span>
												)}
												<span className="rounded-md border border-zinc-800 bg-zinc-900 px-1.5 py-0.5 font-mono text-[11px] text-zinc-400">
													{r.code}
												</span>
												{r.started && (
													<span className="rounded-md border border-zinc-700 bg-zinc-900 px-1.5 py-0.5 text-[11px] text-amber-200/70">
														In game
													</span>
												)}
											</div>
										</div>
										<div className="hidden w-32 sm:block">
											<div className="mb-1 flex items-baseline gap-1 text-sm">
												<span className="font-semibold text-zinc-100">{r.playerCount}</span>
												<span className="text-zinc-500">/</span>
												<span className="text-zinc-400">{unlimited ? "∞" : r.maxPlayers}</span>
											</div>
											<div className="h-1.5 overflow-hidden rounded-full bg-zinc-800">
												<div
													className={`h-full transition-all ${fillColor}`}
													style={{ width: `${fillPct}%` }}
												/>
											</div>
										</div>
										<button
											type="button"
											onClick={() => handleJoinPublic(r.code)}
											disabled={!canSubmit || full}
											title={full ? "Lobby is full" : disabledReason}
											className="h-10 shrink-0 rounded-lg border-0 bg-amber-400/10 px-4 text-sm font-medium text-amber-200 ring-1 ring-inset ring-amber-400/25 transition hover:bg-amber-400/20 hover:text-amber-100 hover:ring-amber-400/40 disabled:cursor-not-allowed disabled:bg-zinc-800/50 disabled:text-zinc-500 disabled:ring-zinc-700"
										>
											{full ? "Full" : "Join"}
										</button>
									</li>
								);
							})}
						</ul>
					)}
				</section>

				<aside className="flex flex-col gap-5 lg:col-span-2">
					<form
						onSubmit={handleCreate}
						className="rounded-2xl border border-zinc-800 bg-zinc-900/60 p-5"
					>
						<h2 className="mb-4 text-base font-medium text-zinc-200">Host a lobby</h2>

						<div className="flex flex-col gap-3">
							<div className="flex flex-col gap-1.5">
								<span className="text-xs font-medium text-zinc-400">Game mode</span>
								<div
									role="radiogroup"
									aria-label="Game mode"
									className="flex h-10 overflow-hidden rounded-lg border border-zinc-800 bg-zinc-950"
								>
									<button
										type="button"
										role="radio"
										aria-checked={mode === "standard"}
										onClick={() => setMode("standard")}
										className={`flex flex-1 items-center justify-center rounded-none border-0 px-3 text-xs font-medium transition ${
											mode === "standard"
												? "bg-zinc-800 text-zinc-100"
												: "bg-transparent text-zinc-400 hover:bg-zinc-900 hover:text-zinc-200"
										}`}
									>
										Standard
									</button>
									<button
										type="button"
										role="radio"
										aria-checked={mode === "grapple_challenge"}
										onClick={() => setMode("grapple_challenge")}
										className={`flex flex-1 items-center justify-center rounded-none border-0 border-l border-l-zinc-800 px-3 text-xs font-medium transition ${
											mode === "grapple_challenge"
												? "bg-emerald-500/20 text-emerald-300"
												: "bg-transparent text-zinc-400 hover:bg-zinc-900 hover:text-zinc-200"
										}`}
									>
										Grapple Challenge
									</button>
								</div>
							</div>

							<label className="flex flex-col gap-1.5">
								<span className="text-xs font-medium text-zinc-400">Lobby name</span>
								<input
									type="text"
									maxLength={48}
									value={displayName}
									onChange={(e) => setDisplayName(e.target.value)}
									placeholder={effectiveDisplayName}
									className="h-10 rounded-lg border border-zinc-800 bg-zinc-950 px-3 text-sm text-zinc-100 outline-none placeholder:text-zinc-500 focus:border-zinc-600"
								/>
							</label>

							<label className="flex flex-col gap-1.5">
								<span className="text-xs font-medium text-zinc-400">Map</span>
								<select
									value={mode === "grapple_challenge" ? "grapple_challenge" : mapId}
									disabled={mode === "grapple_challenge"}
									onChange={(e) => setMapId(e.target.value)}
									className="h-10 w-full rounded-lg border border-zinc-800 bg-zinc-950 px-3 text-sm text-zinc-100 outline-none focus:border-zinc-600 disabled:cursor-default disabled:text-emerald-300 disabled:opacity-100"
								>
									{mode === "grapple_challenge" ? (
										<option value="grapple_challenge">Grapple Challenge</option>
									) : (
										MAPS.map((m) => (
											<option key={m.id} value={m.id}>
												{m.displayName}
											</option>
										))
									)}
								</select>
							</label>

							<div className="grid grid-cols-2 gap-3">
								<label className="flex flex-col gap-1.5">
									<span className="flex items-baseline justify-between text-xs font-medium text-zinc-400">
										<span>Max players</span>
										<span className="text-[10px] font-normal text-zinc-600">−1 = ∞</span>
									</span>
									<input
										type="number"
										min={-1}
										max={64}
										step={1}
										value={maxPlayers}
										onChange={(e) => {
											const n = Number(e.target.value);
											if (!Number.isFinite(n)) return;
											const r = Math.round(n);
											if (r === -1) setMaxPlayers(-1);
											else if (r < 2) setMaxPlayers(2);
											else if (r > 64) setMaxPlayers(64);
											else setMaxPlayers(r);
										}}
										className="h-10 rounded-lg border border-zinc-800 bg-zinc-950 px-3 text-sm text-zinc-100 outline-none focus:border-zinc-600"
									/>
								</label>
								<div className="flex flex-col gap-1.5">
									<span className="text-xs font-medium text-zinc-400">Visibility</span>
									<div
										role="radiogroup"
										aria-label="Lobby visibility"
										className="flex h-10 overflow-hidden rounded-lg border border-zinc-800 bg-zinc-950"
									>
										<button
											type="button"
											role="radio"
											aria-checked={!isPublic}
											onClick={() => setIsPublic(false)}
											className={`flex flex-1 items-center justify-center rounded-none border-0 px-3 text-xs font-medium transition ${
												!isPublic
													? "bg-zinc-800 text-zinc-100"
													: "bg-transparent text-zinc-400 hover:bg-zinc-900 hover:text-zinc-200"
											}`}
										>
											Private
										</button>
										<button
											type="button"
											role="radio"
											aria-checked={isPublic}
											onClick={() => setIsPublic(true)}
											className={`flex flex-1 items-center justify-center rounded-none border-0 border-l border-l-zinc-800 px-3 text-xs font-medium transition ${
												isPublic
													? "bg-emerald-500/20 text-emerald-300"
													: "bg-transparent text-zinc-400 hover:bg-zinc-900 hover:text-zinc-200"
											}`}
										>
											Public
										</button>
									</div>
								</div>
							</div>

							<button
								type="submit"
								disabled={!canSubmit}
								title={disabledReason}
								className="mt-2 h-10 rounded-lg border-0 bg-amber-400/10 px-4 text-sm font-medium text-amber-200 ring-1 ring-inset ring-amber-400/25 transition hover:bg-amber-400/20 hover:text-amber-100 hover:ring-amber-400/40 disabled:cursor-not-allowed disabled:bg-zinc-800/50 disabled:text-zinc-500 disabled:ring-zinc-700"
							>
								Create lobby
							</button>
						</div>
					</form>

					<form
						onSubmit={handleJoin}
						className="rounded-2xl border border-zinc-800 bg-zinc-900/60 p-5"
					>
						<h2 className="mb-3 text-base font-medium text-zinc-200">Join by code</h2>
						<div className="flex gap-2">
							<input
								type="text"
								value={joinCode}
								onChange={(e) => setJoinCode(e.target.value)}
								placeholder="ABC123"
								maxLength={8}
								autoCapitalize="characters"
								spellCheck={false}
								className="h-10 flex-1 rounded-lg border border-zinc-800 bg-zinc-950 px-3 text-center font-mono text-base uppercase tracking-[0.25em] text-zinc-100 outline-none placeholder:text-zinc-600 focus:border-zinc-600"
							/>
							<button
								type="submit"
								disabled={!canSubmit || !joinCode.trim()}
								title={
									!joinCode.trim() && !disabledReason
										? "Enter a room code"
										: disabledReason
								}
								className="h-10 shrink-0 rounded-lg border-0 bg-zinc-800 px-4 text-sm font-semibold text-zinc-100 transition hover:bg-zinc-700 disabled:cursor-not-allowed disabled:opacity-50"
							>
								Join
							</button>
						</div>
					</form>
				</aside>
			</div>

			<ControlsModal open={controlsOpen} onClose={() => setControlsOpen(false)} />
		</main>
	);
}
