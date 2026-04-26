// Cracks open a .srt zip and pulls out the inner `settings.trail`
// binary plus every PNG. Workshop-published .srt files have varying
// folder structures (some have a top-level folder, some don't), so we
// match by basename — anything ending in "settings.trail" wins, and
// every "*.png" comes along.
//
// We use fflate's sync API because .srt files are tiny (30–384 KB)
// and the unzip happens once when the user picks a file (lobby
// thread, no rAF involved). Async would just add Promise overhead.

import { unzipSync, zipSync } from "fflate";

export interface SrtImage {
	name: string;        // basename — e.g. "13.png"
	bytes: Uint8Array;   // raw PNG bytes (browser will Image.decode them later)
}

export interface SrtPayload {
	settings: ArrayBuffer;
	images: SrtImage[];
	// SR's per-trail thumbnail. Lives inside the .srt zip as a stem-less
	// `icon` entry (PNG bytes, no extension). Optional — old user-built
	// zips may omit it.
	icon: Uint8Array | null;
}

function basename(path: string): string {
	const slash = path.lastIndexOf("/");
	return slash >= 0 ? path.slice(slash + 1) : path;
}

export function parseSrt(bytes: Uint8Array): SrtPayload {
	const entries = unzipSync(bytes);

	let settings: Uint8Array | null = null;
	let icon: Uint8Array | null = null;
	const images: SrtImage[] = [];

	for (const [path, entryBytes] of Object.entries(entries)) {
		const name = basename(path);
		if (name === "" || name === ".DS_Store") continue;
		const lower = name.toLowerCase();
		if (lower === "settings.trail") {
			settings = entryBytes;
			continue;
		}
		// SR ships the per-trail thumbnail as a stem-less `icon` entry
		// (PNG bytes, no extension). Bundled presets shipped under
		// `apps/web/public/trails/<id>/icon.png` use the extension —
		// match either so user zips and bundled assets stay symmetric.
		if (lower === "icon" || lower === "icon.png") {
			icon = entryBytes;
			continue;
		}
		if (lower.endsWith(".png")) {
			images.push({ name, bytes: entryBytes });
		}
	}

	if (settings === null) {
		throw new Error("settings.trail not found inside .srt zip");
	}
	if (images.length === 0) {
		throw new Error(".srt zip contains no PNG images");
	}

	// Copy into a freshly-allocated ArrayBuffer so the caller can pass it
	// straight to parseTrail() (which expects an ArrayBuffer, not a typed
	// array view sharing memory with fflate's internal output).
	const ab = new ArrayBuffer(settings.byteLength);
	new Uint8Array(ab).set(settings);

	return { settings: ab, images, icon };
}

// Browser-safe base64 helpers — used to round-trip an .srt blob through
// localStorage (Identity persistence) and the WS relay. Chunked because
// String.fromCharCode.apply blows the stack on large inputs.
export function bytesToBase64(bytes: Uint8Array): string {
	let binary = "";
	const chunk = 0x8000;
	for (let i = 0; i < bytes.length; i += chunk) {
		const slice = bytes.subarray(i, Math.min(i + chunk, bytes.length));
		binary += String.fromCharCode(...slice);
	}
	return btoa(binary);
}

export function base64ToBytes(b64: string): Uint8Array {
	const binary = atob(b64);
	const out = new Uint8Array(binary.length);
	for (let i = 0; i < binary.length; i++) out[i] = binary.charCodeAt(i);
	return out;
}

// Pack a flat list of files into a fresh .srt zip in memory. Used when
// the lobby's folder picker hands us an unzipped trail directory (the
// shape Steam Cloud sync stores them in, under
// `Steam/userdata/<id>/207140/remote/trails/<name>/`). The .srt files
// in `CEngineStorage/AllPlayers/Trails/Local/` are 22-byte stubs and
// can't be used directly.
export function buildSrt(files: readonly { name: string; bytes: Uint8Array }[]): Uint8Array {
	const dir: Record<string, Uint8Array> = {};
	for (const f of files) dir[f.name] = f.bytes;
	return zipSync(dir);
}
