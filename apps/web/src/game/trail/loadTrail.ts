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
	registerImage: (name: string, w: number, h: number, rgba: Uint8Array) => void;
	addLayer: (
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
	const f_register = mod.cwrap("sr_trail_register_image", null,
		["string", "number", "number", "number", "number"]);
	const f_add = mod.cwrap("sr_trail_add_layer", null, [
		"string",
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
		registerImage: (name, w, h, rgba) => {
			// Round-trip through the WASM heap so the C side can read the
			// pixels directly. Free as soon as the upload returns — the
			// texture lives in GL memory from then on.
			const ptr = mod._malloc(rgba.byteLength);
			try {
				mod.HEAPU8.set(rgba, ptr);
				f_register(name, w, h, ptr, rgba.byteLength);
			} finally {
				mod._free(ptr);
			}
		},
		addLayer: (imageName, enabledMode, lifetime, cr, cg, cb, opacity, size,
			fadeOut, fadeOutSpeed, taper, flipH, flipV, forceRightSideUp,
			offX, offY, invertOffset) => {
			f_add(imageName, enabledMode, lifetime, cr, cg, cb, opacity, size,
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

// Pick which trail to load. MVP just hardcodes Goldilocks — once we
// expose a chooser this becomes a per-room setting.
const PREFERRED_ID = "goldilocks";

export async function loadActiveTrail(mod: SrModule): Promise<void> {
	let manifest: TrailManifestEntry[];
	try {
		manifest = await fetchManifest();
	} catch (e) {
		// No manifest = no trails collected on this dev. Silently no-op
		// so the rest of the game still boots.
		console.warn("[trail] manifest unavailable, skipping trail load", e);
		return;
	}
	if (manifest.length === 0) {
		console.warn("[trail] manifest is empty");
		return;
	}

	const entry = manifest.find((m) => m.id === PREFERRED_ID) ?? manifest[0];
	if (!entry) return;

	const abi = bindTrailAbi(mod);
	abi.clear();

	// Settings + image PNGs in parallel (PNGs dominate the wall-clock).
	const [settingsBuf, imageResults] = await Promise.all([
		fetchSettings(entry.id, entry.settings),
		Promise.all(entry.images.map(async (name) => {
			const out = await decodePng(`${BASE}trails/${entry.id}/${name}`);
			return { name, ...out };
		})),
	]);

	for (const im of imageResults) {
		abi.registerImage(im.name, im.w, im.h, im.rgba);
	}

	const def = parseTrail(settingsBuf);
	for (const L of def.layers) {
		// Skip explicitly-hidden layers — same behavior as the SR editor's
		// Visible toggle.
		if (!L.visible) continue;
		// Layers that reference images we don't have are dropped silently
		// (the C side would no-op them anyway, but skipping cuts a few
		// wasted ABI calls per trail).
		if (!imageResults.find((i) => i.name === L.imageName)) continue;

		abi.addLayer(
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

	console.log(`[trail] loaded "${def.name}" (${def.layers.length} layers, ${imageResults.length} images)`);
}
