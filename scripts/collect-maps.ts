#!/usr/bin/env bun
// Find the 4 target SpeedRunners maps in the local Steam install and
// copy them into game/assets/maps/ with normalized names.
//
// Workshop maps live under (macOS):
//   ~/Library/Application Support/SpeedRunners/CEngineStorage/AllPlayers/Subscribed
//
// Filenames follow the pattern:
//   <steamId>.<itemId>.<DisplayName>.sr
//
// We match by suffix (case-insensitive). If multiple files match a target,
// we pick the most recently modified.

import { readdir, stat, mkdir, copyFile, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { join, basename } from "node:path";

type MapTarget = {
	id: string;
	displayName: string;
	suffix: string; // case-insensitive filename suffix to match
};

const TARGETS: MapTarget[] = [
	{ id: "pitfall", displayName: "Pitfall", suffix: "Pitfall.sr" },
	{ id: "genetics", displayName: "Genetics", suffix: "Genetics.sr" },
	{
		id: "grapple_circuit",
		displayName: "Grapple Circuit",
		suffix: "Grapple Circuit.sr",
	},
	{
		id: "oasis_abyss",
		displayName: "Oasis Abyss",
		suffix: "Oasis - Abyss.sr",
	},
];

const SUBSCRIBED = join(
	homedir(),
	"Library/Application Support/SpeedRunners/CEngineStorage/AllPlayers/Subscribed",
);

const OUT_DIR = join(import.meta.dir, "..", "game", "assets", "maps");

type ManifestEntry = {
	id: string;
	displayName: string;
	file: string;
};

async function newestMatch(
	dir: string,
	suffixLower: string,
): Promise<string | null> {
	const entries = await readdir(dir);
	let best: { path: string; mtime: number } | null = null;
	for (const name of entries) {
		if (!name.toLowerCase().endsWith(suffixLower)) continue;
		const full = join(dir, name);
		const s = await stat(full);
		if (!s.isFile()) continue;
		if (best === null || s.mtimeMs > best.mtime) {
			best = { path: full, mtime: s.mtimeMs };
		}
	}
	return best?.path ?? null;
}

async function main(): Promise<void> {
	try {
		const s = await stat(SUBSCRIBED);
		if (!s.isDirectory()) throw new Error("not a directory");
	} catch {
		console.error(`[collect-maps] Subscribed folder not found:\n  ${SUBSCRIBED}`);
		console.error(
			`[collect-maps] Install SpeedRunners on Steam and subscribe to the 4 target maps.`,
		);
		process.exit(1);
	}

	await mkdir(OUT_DIR, { recursive: true });

	const manifest: ManifestEntry[] = [];
	const missing: string[] = [];

	for (const target of TARGETS) {
		const src = await newestMatch(SUBSCRIBED, target.suffix.toLowerCase());
		if (src === null) {
			console.warn(`[collect-maps] MISSING: ${target.displayName}`);
			missing.push(target.displayName);
			continue;
		}
		const destFile = `${target.id}.sr`;
		const dest = join(OUT_DIR, destFile);
		await copyFile(src, dest);
		console.log(`[collect-maps] ${target.displayName.padEnd(18)} ← ${basename(src)}`);
		manifest.push({
			id: target.id,
			displayName: target.displayName,
			file: destFile,
		});
	}

	const manifestPath = join(OUT_DIR, "manifest.json");
	await writeFile(manifestPath, `${JSON.stringify(manifest, null, "\t")}\n`);
	console.log(`[collect-maps] wrote ${manifestPath}`);

	if (missing.length > 0) {
		console.error(
			`[collect-maps] ${missing.length} map(s) missing — they will not be selectable in the lobby.`,
		);
		process.exit(missing.length === TARGETS.length ? 1 : 0);
	}
}

await main();
