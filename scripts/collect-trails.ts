#!/usr/bin/env bun
// Pull SpeedRunners workshop trails into apps/web/public/trails/ so Vite
// can serve them as static assets at runtime. We copy the raw .trail
// binary plus all PNG images so the JS-side parser + texture upload don't
// need to talk to the local Steam install.
//
// Workshop trails live under (macOS):
//   ~/Library/Application Support/Steam/steamapps/common/SpeedRunners/
//     SpeedRunners.app/Contents/Resources/WorkshopContent/<itemId>/
//
// Each itemId dir contains: settings.trail + N PNGs + an `icon` blob.
// We don't try to be cute about which PNGs are referenced — just copy
// every .png and let the parser pick. The .trail file's Image properties
// reference filenames by base name (e.g. "13.png").
//
// MVP: hardcoded Goldilocks (workshop ID 3230477673). Later targets get
// added to TARGETS below.

import { readdir, stat, mkdir, copyFile, writeFile, rm } from "node:fs/promises";
import { homedir } from "node:os";
import { join } from "node:path";

interface TrailTarget {
	id: string;
	displayName: string;
	workshopId: string;
}

const TARGETS: TrailTarget[] = [
	{ id: "goldilocks", displayName: "ST Goldilocks", workshopId: "3230477673" },
];

const WORKSHOP_ROOT = join(
	homedir(),
	"Library/Application Support/Steam/steamapps/common/SpeedRunners",
	"SpeedRunners.app/Contents/Resources/WorkshopContent",
);

const OUT_ROOT = join(import.meta.dir, "..", "apps", "web", "public", "trails");

interface ManifestEntry {
	id: string;
	displayName: string;
	settings: string;
	images: string[];
}

async function dirExists(path: string): Promise<boolean> {
	try {
		const s = await stat(path);
		return s.isDirectory();
	} catch {
		return false;
	}
}

async function collect(target: TrailTarget): Promise<ManifestEntry | null> {
	const src = join(WORKSHOP_ROOT, target.workshopId);
	if (!(await dirExists(src))) {
		console.warn(`[collect-trails] MISSING ${target.displayName} (workshop ${target.workshopId})`);
		return null;
	}

	const entries = await readdir(src);
	const settings = entries.find((n) => n === "settings.trail");
	if (!settings) {
		console.warn(`[collect-trails] ${target.displayName}: no settings.trail in ${src}`);
		return null;
	}

	const out = join(OUT_ROOT, target.id);
	// Wipe + recreate so removed PNGs don't linger across collects.
	await rm(out, { recursive: true, force: true });
	await mkdir(out, { recursive: true });

	await copyFile(join(src, settings), join(out, "settings.trail"));

	const images: string[] = [];
	for (const name of entries) {
		if (!name.toLowerCase().endsWith(".png")) continue;
		await copyFile(join(src, name), join(out, name));
		images.push(name);
	}

	console.log(`[collect-trails] ${target.displayName.padEnd(18)} ← ${target.workshopId} (${images.length} PNGs)`);

	return {
		id: target.id,
		displayName: target.displayName,
		settings: "settings.trail",
		images,
	};
}

async function main(): Promise<void> {
	if (!(await dirExists(WORKSHOP_ROOT))) {
		console.error(`[collect-trails] Workshop folder not found:\n  ${WORKSHOP_ROOT}`);
		console.error(`[collect-trails] Install SpeedRunners on Steam and subscribe to the target trails.`);
		process.exit(1);
	}

	await mkdir(OUT_ROOT, { recursive: true });

	const manifest: ManifestEntry[] = [];
	const missing: string[] = [];
	for (const t of TARGETS) {
		const entry = await collect(t);
		if (entry === null) {
			missing.push(t.displayName);
			continue;
		}
		manifest.push(entry);
	}

	const manifestPath = join(OUT_ROOT, "manifest.json");
	await writeFile(manifestPath, `${JSON.stringify(manifest, null, "\t")}\n`);
	console.log(`[collect-trails] wrote ${manifestPath}`);

	if (missing.length > 0) {
		console.error(`[collect-trails] ${missing.length} trail(s) missing — they will not be loaded in-game.`);
		process.exit(missing.length === TARGETS.length ? 1 : 0);
	}
}

await main();
