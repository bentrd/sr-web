#!/usr/bin/env bun
// Pull SpeedRunners trails into apps/web/public/trails/ so Vite can serve
// them as static assets at runtime. Two source flavours are supported:
//
//   - "workshop": ~/Library/Application Support/Steam/steamapps/common/
//                 SpeedRunners/SpeedRunners.app/Contents/Resources/
//                 WorkshopContent/<itemId>/
//   - "userdata": ~/Library/Application Support/Steam/userdata/
//                 <accountId>/207140/remote/trails/<folderName>/
//                 (auto-detected — first numeric child of userdata/ wins)
//
// Each trail dir contains: settings.trail + N PNGs + an `icon` blob (also
// PNG). We copy every .png by basename and rename `icon` → `icon.png` so
// static hosts serve the correct MIME type without a custom rule. The
// .trail file's Image properties reference filenames by base name
// (e.g. "13.png").

import { readdir, stat, mkdir, copyFile, writeFile, rm } from "node:fs/promises";
import { homedir } from "node:os";
import { join } from "node:path";

type TrailTarget =
	| { id: string; displayName: string; source: "workshop"; workshopId: string }
	| { id: string; displayName: string; source: "userdata"; folderName: string };

const TARGETS: TrailTarget[] = [
	{ id: "goldilocks", displayName: "ST Goldilocks", source: "workshop", workshopId: "3230477673" },
	{ id: "orange-superspeed", displayName: "Orange Superspeed", source: "userdata", folderName: "Orange Superspeed Trail" },
];

const STEAM_ROOT = join(homedir(), "Library/Application Support/Steam");
const WORKSHOP_ROOT = join(
	STEAM_ROOT,
	"steamapps/common/SpeedRunners",
	"SpeedRunners.app/Contents/Resources/WorkshopContent",
);
const USERDATA_ROOT = join(STEAM_ROOT, "userdata");

const OUT_ROOT = join(import.meta.dir, "..", "apps", "web", "public", "trails");

interface ManifestEntry {
	id: string;
	displayName: string;
	settings: string;
	icon: string;
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

async function fileExists(path: string): Promise<boolean> {
	try {
		const s = await stat(path);
		return s.isFile();
	} catch {
		return false;
	}
}

// Find the first per-account `userdata/<accountId>/207140/remote/trails`
// subfolder. SR's Steam app id is 207140. Most installs only have one
// account; if there are multiple we just pick the first numeric one.
let userdataTrailsRootCache: string | null | undefined;
async function findUserdataTrailsRoot(): Promise<string | null> {
	if (userdataTrailsRootCache !== undefined) return userdataTrailsRootCache;
	if (!(await dirExists(USERDATA_ROOT))) {
		userdataTrailsRootCache = null;
		return null;
	}
	const accounts = await readdir(USERDATA_ROOT);
	for (const a of accounts) {
		if (!/^\d+$/.test(a)) continue;
		const candidate = join(USERDATA_ROOT, a, "207140", "remote", "trails");
		if (await dirExists(candidate)) {
			userdataTrailsRootCache = candidate;
			return candidate;
		}
	}
	userdataTrailsRootCache = null;
	return null;
}

async function resolveSource(target: TrailTarget): Promise<string | null> {
	if (target.source === "workshop") {
		const src = join(WORKSHOP_ROOT, target.workshopId);
		return (await dirExists(src)) ? src : null;
	}
	const root = await findUserdataTrailsRoot();
	if (!root) return null;
	const src = join(root, target.folderName);
	return (await dirExists(src)) ? src : null;
}

async function collect(target: TrailTarget): Promise<ManifestEntry | null> {
	const src = await resolveSource(target);
	if (!src) {
		const where =
			target.source === "workshop"
				? `workshop ${target.workshopId}`
				: `userdata "${target.folderName}"`;
		console.warn(`[collect-trails] MISSING ${target.displayName} (${where})`);
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

	// Copy `icon` (no extension in SR's layout) as `icon.png` so static
	// hosts serve image/png. If the source has no icon we still ship
	// an entry so the manifest schema stays uniform — the dropdown
	// falls back to a placeholder when the file is missing.
	let icon: string | null = null;
	if (await fileExists(join(src, "icon"))) {
		await copyFile(join(src, "icon"), join(out, "icon.png"));
		icon = "icon.png";
	}

	const images: string[] = [];
	for (const name of entries) {
		// Skip the icon (already handled above) and any non-PNG. Steam
		// Cloud sync occasionally leaves a `.png` file with an empty
		// stem in userdata folders — `.trail` references PNGs by base
		// name, so a stem-less file can't be addressed and is junk.
		if (name === "" || name === "icon") continue;
		if (!name.toLowerCase().endsWith(".png")) continue;
		if (name.toLowerCase() === ".png") continue;
		await copyFile(join(src, name), join(out, name));
		images.push(name);
	}

	console.log(
		`[collect-trails] ${target.displayName.padEnd(20)} ← ${target.source.padEnd(8)} (${images.length} PNG${images.length === 1 ? "" : "s"}${icon ? " + icon" : ""})`,
	);

	return {
		id: target.id,
		displayName: target.displayName,
		settings: "settings.trail",
		icon: icon ?? "",
		images,
	};
}

async function main(): Promise<void> {
	const haveWorkshop = await dirExists(WORKSHOP_ROOT);
	const haveUserdata = (await findUserdataTrailsRoot()) !== null;
	if (!haveWorkshop && !haveUserdata) {
		console.error(`[collect-trails] No SR sources found. Looked in:`);
		console.error(`  workshop: ${WORKSHOP_ROOT}`);
		console.error(`  userdata: ${USERDATA_ROOT}/<accountId>/207140/remote/trails`);
		console.error(`[collect-trails] Install SpeedRunners on Steam and subscribe / create the target trails.`);
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
