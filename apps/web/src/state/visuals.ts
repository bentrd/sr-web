// User-tunable rendering palette + grapple-head size + render FPS cap.
// Colors are stored as 0..1 RGB tuples to mirror the C++ side
// (sr_set_visual_*); the OptionsModal converts to/from #rrggbb hex.

export type ColorRgb = readonly [number, number, number];

export interface Visuals {
	bg: ColorRgb;
	walls: ColorRgb;
	grappleStripe: ColorRgb;
	wallclimbStripe: ColorRgb;
	grappleCord: ColorRgb;
	grappleHead: ColorRgb;
	grappleHeadSize: number;
}

export const VISUAL_DEFAULTS: Visuals = {
	bg: [0.16, 0.17, 0.2],
	walls: [0.62, 0.64, 0.68],
	grappleStripe: [1, 1, 1],
	wallclimbStripe: [1, 1, 1],
	grappleCord: [0, 0, 0],
	grappleHead: [1, 0, 0],
	grappleHeadSize: 12,
};

export const HEAD_SIZE_MIN = 1;
export const HEAD_SIZE_MAX = 64;

const STORAGE_KEY = "sr-web.visuals";

function isColor(v: unknown): v is ColorRgb {
	return (
		Array.isArray(v) &&
		v.length === 3 &&
		v.every((c) => typeof c === "number" && Number.isFinite(c) && c >= 0 && c <= 1)
	);
}

function clampSize(n: number): number {
	if (!Number.isFinite(n)) return VISUAL_DEFAULTS.grappleHeadSize;
	return Math.max(HEAD_SIZE_MIN, Math.min(HEAD_SIZE_MAX, n));
}

export function loadVisuals(): Visuals {
	try {
		const raw = localStorage.getItem(STORAGE_KEY);
		if (!raw) return VISUAL_DEFAULTS;
		const parsed = JSON.parse(raw) as Partial<Visuals>;
		return {
			bg: isColor(parsed.bg) ? parsed.bg : VISUAL_DEFAULTS.bg,
			walls: isColor(parsed.walls) ? parsed.walls : VISUAL_DEFAULTS.walls,
			grappleStripe: isColor(parsed.grappleStripe) ? parsed.grappleStripe : VISUAL_DEFAULTS.grappleStripe,
			wallclimbStripe: isColor(parsed.wallclimbStripe) ? parsed.wallclimbStripe : VISUAL_DEFAULTS.wallclimbStripe,
			grappleCord: isColor(parsed.grappleCord) ? parsed.grappleCord : VISUAL_DEFAULTS.grappleCord,
			grappleHead: isColor(parsed.grappleHead) ? parsed.grappleHead : VISUAL_DEFAULTS.grappleHead,
			grappleHeadSize: clampSize(
				typeof parsed.grappleHeadSize === "number" ? parsed.grappleHeadSize : VISUAL_DEFAULTS.grappleHeadSize,
			),
		};
	} catch {
		return VISUAL_DEFAULTS;
	}
}

export function saveVisuals(v: Visuals): void {
	try {
		localStorage.setItem(STORAGE_KEY, JSON.stringify(v));
	} catch {
		// localStorage may be disabled — non-fatal, settings just won't persist.
	}
}

export function colorToHex([r, g, b]: ColorRgb): string {
	const c = (n: number): string =>
		Math.max(0, Math.min(255, Math.round(n * 255)))
			.toString(16)
			.padStart(2, "0");
	return `#${c(r)}${c(g)}${c(b)}`;
}

export function hexToColor(hex: string): ColorRgb {
	const m = /^#?([0-9a-f]{6})$/i.exec(hex);
	if (!m) return [0, 0, 0];
	const v = m[1]!;
	const r = parseInt(v.slice(0, 2), 16) / 255;
	const g = parseInt(v.slice(2, 4), 16) / 255;
	const b = parseInt(v.slice(4, 6), 16) / 255;
	return [r, g, b];
}
