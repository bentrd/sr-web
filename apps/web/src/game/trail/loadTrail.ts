// Fetch a workshop trail from /trails/<id>/, parse the .trail binary, decode
// the referenced PNGs in the browser, and push the result down the C ABI.
//
// PNG decoding stays in JS because (a) the browser already ships a PNG
// decoder and (b) the C side stays tiny — no libpng / lodepng dependency.
// We resolve image bytes to a tightly packed RGBA buffer via a 2D canvas
// and copy it into WASM memory only long enough for sr_trail_register_image
// to upload it as a GL texture.

import type { SrModule } from "../../wasm/loadModule";
import { parseTrail, type TrailLayer } from "./parseTrail";
import { parseSrt } from "./parseSrt";

export interface TrailManifestEntry {
	id: string;
	displayName: string;
	settings: string;
	images: string[];
}

const BASE = import.meta.env.BASE_URL;
const MANIFEST_URL = `${BASE}trails/manifest.json`;

interface TrailAbi {
	clear: () => void;
	clearTrack: (trackId: string) => void;
	setTrackOpacity: (trackId: string, opacity: number) => void;
	setTrackVisible: (trackId: string, visible: boolean) => void;
	registerImage: (trackId: string, name: string, w: number, h: number, rgba: Uint8Array) => void;
	addLayer: (
		trackId: string,
		imageName: string,
		enabledMode: number,
		lifetime: number,
		colorR: number, colorG: number, colorB: number,
		opacity: number,
		size: number,
		fadeOut: number, fadeOutSpeed: number,
		taper: number,
		flipH: number, flipV: number, forceRightSideUp: number,
		offsetX: number, offsetY: number, invertOffset: number,
	) => void;
}

export function bindTrailAbi(mod: SrModule): TrailAbi {
	const f_clear = mod.cwrap("sr_trail_clear", null, []);
	const f_clear_track = mod.cwrap("sr_trail_clear_track", null, ["string"]);
	const f_set_opacity = mod.cwrap("sr_trail_set_track_opacity", null, ["string", "number"]);
	const f_set_visible = mod.cwrap("sr_trail_set_track_visible", null, ["string", "number"]);
	const f_register = mod.cwrap("sr_trail_register_image", null,
		["string", "string", "number", "number", "number", "number"]);
	const f_add = mod.cwrap("sr_trail_add_layer", null, [
		"string",                         // track_id
		"string",                         // image_name
		"number",                         // enabled_mode
		"number",                         // lifetime
		"number", "number", "number",     // color
		"number",                         // opacity
		"number",                         // size
		"number", "number",               // fade_out, fade_out_speed
		"number",                         // taper
		"number", "number", "number",     // flip_h, flip_v, force_right_side_up
		"number", "number",               // offset
		"number",                         // invert_offset
	]);

	return {
		clear: () => { f_clear(); },
		clearTrack: (trackId) => { f_clear_track(trackId); },
		setTrackOpacity: (trackId, opacity) => { f_set_opacity(trackId, opacity); },
		setTrackVisible: (trackId, visible) => { f_set_visible(trackId, visible ? 1 : 0); },
		registerImage: (trackId, name, w, h, rgba) => {
			const ptr = mod._malloc(rgba.byteLength);
			try {
				mod.HEAPU8.set(rgba, ptr);
				f_register(trackId, name, w, h, ptr, rgba.byteLength);
			} finally {
				mod._free(ptr);
			}
		},
		addLayer: (trackId, imageName, enabledMode, lifetime, cr, cg, cb, opacity, size,
			fadeOut, fadeOutSpeed, taper, flipH, flipV, forceRightSideUp,
			offX, offY, invertOffset) => {
			f_add(trackId, imageName, enabledMode, lifetime, cr, cg, cb, opacity, size,
				fadeOut, fadeOutSpeed, taper, flipH, flipV, forceRightSideUp,
				offX, offY, invertOffset);
		},
	};
}

async function fetchManifest(): Promise<TrailManifestEntry[]> {
	const res = await fetch(MANIFEST_URL);
	if (!res.ok) throw new Error(`trail manifest fetch failed: ${res.status}`);
	return res.json() as Promise<TrailManifestEntry[]>;
}

async function fetchSettings(id: string, file: string): Promise<ArrayBuffer> {
	const res = await fetch(`${BASE}trails/${id}/${file}`);
	if (!res.ok) throw new Error(`trail settings fetch failed: ${res.status}`);
	return res.arrayBuffer();
}

// Decode a PNG via the browser's image pipeline + a 2D canvas. Returns
// tightly-packed RGBA8 + dimensions. We can't trust Image.naturalWidth
// before await `decode()` — Safari occasionally returns 0 if the bitmap
// hasn't been parsed yet.
async function decodePng(url: string): Promise<{ w: number; h: number; rgba: Uint8Array }> {
	const img = new Image();
	img.crossOrigin = "anonymous";
	img.src = url;
	await img.decode();
	const w = img.naturalWidth;
	const h = img.naturalHeight;
	if (w <= 0 || h <= 0) throw new Error(`PNG decode returned zero size: ${url}`);

	const canvas = document.createElement("canvas");
	canvas.width = w;
	canvas.height = h;
	const ctx = canvas.getContext("2d", { willReadFrequently: false });
	if (!ctx) throw new Error("2D canvas context unavailable");
	ctx.drawImage(img, 0, 0);
	const data = ctx.getImageData(0, 0, w, h).data;
	// Copy out of the ImageData buffer so the canvas can be GC'd. The
	// buffer also gets released when we leave this scope; downstream owns
	// the Uint8Array.
	return { w, h, rgba: new Uint8Array(data.buffer.slice(0)) };
}

const ENABLED_MODE_ALWAYS = 0;
const ENABLED_MODE_SUPERSPEED = 1;

function modeForLayer(L: TrailLayer): number {
	return L.enabledMode === "ONLY AT SUPERSPEED" ? ENABLED_MODE_SUPERSPEED : ENABLED_MODE_ALWAYS;
}

// Apply a parsed trail to a specific track. Used by both the default
// manifest loader and the .srt loader — they only differ in where
// the bytes/images come from.
async function applyTrailToTrack(
	mod: SrModule,
	trackId: string,
	settingsBuf: ArrayBuffer,
	imageResults: { name: string; w: number; h: number; rgba: Uint8Array }[],
): Promise<void> {
	const abi = bindTrailAbi(mod);
	abi.clearTrack(trackId);

	for (const im of imageResults) {
		abi.registerImage(trackId, im.name, im.w, im.h, im.rgba);
	}

	const def = parseTrail(settingsBuf);
	for (const L of def.layers) {
		if (!L.visible) continue;
		if (!imageResults.find((i) => i.name === L.imageName)) continue;

		abi.addLayer(
			trackId,
			L.imageName,
			modeForLayer(L),
			L.lifetime,
			L.color[0], L.color[1], L.color[2],
			L.opacity,
			L.size,
			L.fadeOut ? 1 : 0, L.fadeOutSpeed,
			L.taper ? 1 : 0,
			L.flipH ? 1 : 0, L.flipV ? 1 : 0, L.forceRightSideUp ? 1 : 0,
			L.offsetVector[0], L.offsetVector[1], L.invertOffset ? 1 : 0,
		);
	}

	console.log(`[trail] loaded "${def.name}" into track "${trackId}" (${def.layers.length} layers, ${imageResults.length} images)`);
}

// Load the bundled default trail (Goldilocks if present) into the given track.
// Used as a fallback when the user hasn't picked a custom .srt.
export async function loadDefaultTrail(mod: SrModule, trackId: string): Promise<void> {
	let manifest: TrailManifestEntry[];
	try {
		manifest = await fetchManifest();
	} catch (e) {
		console.warn("[trail] manifest unavailable, skipping trail load", e);
		return;
	}
	if (manifest.length === 0) {
		console.warn("[trail] manifest is empty");
		return;
	}

	const entry = manifest.find((m) => m.id === PREFERRED_ID) ?? manifest[0];
	if (!entry) return;

	const [settingsBuf, imageResults] = await Promise.all([
		fetchSettings(entry.id, entry.settings),
		Promise.all(entry.images.map(async (name) => {
			const out = await decodePng(`${BASE}trails/${entry.id}/${name}`);
			return { name, ...out };
		})),
	]);

	await applyTrailToTrack(mod, trackId, settingsBuf, imageResults);
}

// Load a user-picked .srt blob into the given track. Used for the
// local player's chosen trail and (via WS replay) for each peer.
export async function loadTrailFromBytes(
	mod: SrModule,
	trackId: string,
	srtBytes: Uint8Array,
): Promise<void> {
	const payload = parseSrt(srtBytes);

	// Decode every PNG via the same canvas pipeline the manifest path uses.
	// We round-trip each PNG through a blob URL so decodePng's <img>
	// pipeline works unchanged.
	const imageResults = await Promise.all(payload.images.map(async (im) => {
		const blob = new Blob([new Uint8Array(im.bytes)], { type: "image/png" });
		const url = URL.createObjectURL(blob);
		try {
			const decoded = await decodePng(url);
			return { name: im.name, ...decoded };
		} finally {
			URL.revokeObjectURL(url);
		}
	}));

	await applyTrailToTrack(mod, trackId, payload.settings, imageResults);
}

// Pick which trail to load. MVP just hardcodes Goldilocks — once we
// expose a chooser this becomes a per-room setting.
const PREFERRED_ID = "goldilocks";
