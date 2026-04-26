// Fetch a bundled preset trail from /trails/<id>/ and pack it into the
// same .srt blob shape that user uploads produce. This keeps Game.tsx's
// trail-loading path uniform: identity.trail is always a base64 .srt
// regardless of where it came from.

import { buildSrt } from "./parseSrt";
import type { TrailManifestEntry } from "./loadTrail";

const BASE = import.meta.env.BASE_URL;

export interface PresetBytes {
	bytes: Uint8Array;
	iconDataUrl?: string;
}

async function fetchBytes(url: string): Promise<Uint8Array> {
	const res = await fetch(url);
	if (!res.ok) throw new Error(`fetch ${url} -> ${res.status}`);
	return new Uint8Array(await res.arrayBuffer());
}

export async function fetchPresetAsSrt(entry: TrailManifestEntry): Promise<PresetBytes> {
	const dir = `${BASE}trails/${entry.id}`;

	const files: { name: string; bytes: Uint8Array }[] = [];

	// settings.trail (required)
	files.push({
		name: "settings.trail",
		bytes: await fetchBytes(`${dir}/${entry.settings}`),
	});

	// PNG layer images (parsed in parallel; small files, browser will
	// happily pipeline a handful of static fetches).
	const imageBytes = await Promise.all(
		entry.images.map(async (name) => ({ name, bytes: await fetchBytes(`${dir}/${name}`) })),
	);
	for (const f of imageBytes) files.push(f);

	let iconDataUrl: string | undefined;
	if (entry.icon) {
		const iconBytes = await fetchBytes(`${dir}/${entry.icon}`);
		// Store the icon stem-less inside the zip so parseSrt's existing
		// `icon | icon.png` matcher picks it up regardless of which path
		// (preset or user upload) produced the blob.
		files.push({ name: "icon", bytes: iconBytes });
		iconDataUrl = bytesToPngDataUrl(iconBytes);
	}

	return { bytes: buildSrt(files), iconDataUrl };
}

// Cheap PNG-bytes → data URL conversion. Kept inline because the icon
// blobs are tiny (a few KB) and FileReader's async indirection isn't
// worth the round trip for what's effectively one render-time string.
export function bytesToPngDataUrl(bytes: Uint8Array): string {
	let binary = "";
	const chunk = 0x8000;
	for (let i = 0; i < bytes.length; i += chunk) {
		const slice = bytes.subarray(i, Math.min(i + chunk, bytes.length));
		binary += String.fromCharCode(...slice);
	}
	return `data:image/png;base64,${btoa(binary)}`;
}
