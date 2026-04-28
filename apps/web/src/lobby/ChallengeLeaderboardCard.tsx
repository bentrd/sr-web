import { useEffect, useState } from "react";
import { useApp } from "../state/AppState";
import type { ServerMsg } from "@sr-web/protocol";

type LeaderboardEntry = { rank: number; name: string; value: number; display: string };

type ChallengeConfig = {
	key: string;
	label: string;
	url: string;
	valueKey: string;
	unit: string;
	format?: (n: number) => string;
};

// 300 ticks/sec → seconds with 3 decimals → e.g. "12.345".
function formatTicksAsSeconds(ticks: number): string {
	return (ticks / 300).toFixed(3);
}

const CHALLENGES: ChallengeConfig[] = [
	{ key: "speed", label: "Speed Challenge", url: "/leaderboard?limit=10", valueKey: "maxSpeed", unit: "wu/s" },
	{ key: "rg", label: "RG Challenge", url: "/rg-leaderboard?limit=10", valueKey: "maxStreak", unit: "RGs" },
	{ key: "time", label: "Time Challenge", url: "/time-leaderboard?limit=10", valueKey: "durationTicks", unit: "sec", format: formatTicksAsSeconds },
];

function getLeaderboardUrl(path: string): string {
	const wsUrl = (import.meta as { env?: { VITE_WS_URL?: string } }).env?.VITE_WS_URL ?? "ws://localhost:4000/ws";
	return wsUrl.replace(/^ws/, "http").replace(/\/ws$/, path);
}

export function ChallengeLeaderboardCard(): JSX.Element {
	const [data, setData] = useState<Record<string, LeaderboardEntry[]>>({});
	const [loading, setLoading] = useState(true);
	const { ws } = useApp();

	useEffect(() => {
		let cancelled = false;
		async function fetchAll(): Promise<void> {
			setLoading(true);
			const results: Record<string, LeaderboardEntry[]> = {};
			for (const ch of CHALLENGES) {
				try {
					const res = await fetch(getLeaderboardUrl(ch.url));
					if (!res.ok) throw new Error(`HTTP ${res.status}`);
					const rows = (await res.json()) as Array<Record<string, unknown>>;
					results[ch.key] = rows.map((r, i) => {
						const value = (r[ch.valueKey] as number) ?? 0;
						return {
							rank: (r.rank as number) ?? i + 1,
							name: r.name as string,
							value,
							display: ch.format ? ch.format(value) : String(value),
						};
					});
				} catch {
					results[ch.key] = [];
				}
			}
			if (!cancelled) {
				setData(results);
				setLoading(false);
			}
		}
		void fetchAll();

		// Live updates via WebSocket — the server publishes global
		// leaderboard updates whenever anyone submits a score.
		const off = ws.onMessage((msg: ServerMsg) => {
			if (msg.type === "leaderboard") {
				setData((prev) => ({
					...prev,
					speed: msg.entries.map((e) => ({
						rank: e.rank,
						name: e.name,
						value: e.maxSpeed,
						display: String(e.maxSpeed),
					})),
				}));
			} else if (msg.type === "rg_leaderboard") {
				setData((prev) => ({
					...prev,
					rg: msg.entries.map((e) => ({
						rank: e.rank,
						name: e.name,
						value: e.maxStreak,
						display: String(e.maxStreak),
					})),
				}));
			} else if (msg.type === "time_leaderboard") {
				setData((prev) => ({
					...prev,
					time: msg.entries.map((e) => ({
						rank: e.rank,
						name: e.name,
						value: e.durationTicks,
						display: formatTicksAsSeconds(e.durationTicks),
					})),
				}));
			}
		});

		return () => {
			cancelled = true;
			off();
		};
	}, [ws]);

	return (
		<section className="rounded-2xl border border-zinc-800 bg-zinc-900/60 p-5">
			<div className="mb-4">
				<h2 className="text-base font-medium text-zinc-200">All-Time Leaderboards</h2>
				<div className="text-xs text-zinc-400">Challenge mode rankings</div>
			</div>

			{loading ? (
				<div className="py-8 text-center text-xs text-zinc-500">Loading…</div>
			) : (
				<div className="flex flex-col gap-6 sm:flex-row">
					{CHALLENGES.map((ch) => {
						const entries = data[ch.key] ?? [];
						return (
							<div key={ch.key} className="flex min-w-0 flex-1 flex-col">
								<div className="mb-2 text-xs font-semibold uppercase tracking-wider text-zinc-400">
									{ch.label}
								</div>
								{entries.length === 0 ? (
									<div className="py-4 text-center text-xs text-zinc-500">No scores yet.</div>
								) : (
									<table className="w-full table-fixed border-collapse">
										<thead>
											<tr>
												<th className="w-8 px-1 py-1.5 text-left text-[10px] font-semibold uppercase tracking-wider text-zinc-500">#</th>
												<th className="px-1 py-1.5 text-left text-[10px] font-semibold uppercase tracking-wider text-zinc-500">Name</th>
												<th className="w-16 px-1 py-1.5 text-right text-[10px] font-semibold uppercase tracking-wider text-zinc-500">{ch.unit}</th>
											</tr>
										</thead>
										<tbody>
											{entries.map((e) => (
												<tr key={e.rank} className="border-t border-zinc-800/50">
													<td className="truncate px-1 py-1.5 text-xs font-semibold text-amber-400">{e.rank}</td>
													<td className="truncate px-1 py-1.5 text-xs text-zinc-300">{e.name}</td>
													<td className="whitespace-nowrap px-1 py-1.5 text-right font-mono text-xs font-semibold text-zinc-100">{e.display}</td>
												</tr>
											))}
										</tbody>
									</table>
								)}
							</div>
						);
					})}
				</div>
			)}
		</section>
	);
}