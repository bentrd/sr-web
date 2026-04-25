import type { RGB } from "@sr-web/protocol";

// HTML <input type="color"> uses 8-bit hex (#rrggbb).
// The protocol uses normalised floats in [0, 1] so the C++ side can
// pass them straight to glClearColor / shader uniforms without conversion.

export function hexToRgb(hex: string): RGB {
	const s = hex.replace(/^#/, "").padEnd(6, "0").slice(0, 6);
	const r = parseInt(s.slice(0, 2), 16) / 255;
	const g = parseInt(s.slice(2, 4), 16) / 255;
	const b = parseInt(s.slice(4, 6), 16) / 255;
	return [r, g, b];
}

export function rgbToHex(rgb: RGB): string {
	const to = (v: number): string =>
		Math.round(Math.max(0, Math.min(1, v)) * 255)
			.toString(16)
			.padStart(2, "0");
	return `#${to(rgb[0])}${to(rgb[1])}${to(rgb[2])}`;
}

export function rgbToCss(rgb: RGB, alpha = 1): string {
	const r = Math.round(rgb[0] * 255);
	const g = Math.round(rgb[1] * 255);
	const b = Math.round(rgb[2] * 255);
	return alpha < 1
		? `rgba(${r}, ${g}, ${b}, ${alpha})`
		: `rgb(${r}, ${g}, ${b})`;
}

// Pick a random saturated, bright color so two players that don't open
// the picker are unlikely to collide.
export function randomColor(): RGB {
	const h = Math.random();
	const s = 0.75;
	const v = 0.95;
	const i = Math.floor(h * 6);
	const f = h * 6 - i;
	const p = v * (1 - s);
	const q = v * (1 - f * s);
	const t = v * (1 - (1 - f) * s);
	switch (i % 6) {
		case 0: return [v, t, p];
		case 1: return [q, v, p];
		case 2: return [p, v, t];
		case 3: return [p, q, v];
		case 4: return [t, p, v];
		default: return [v, p, q];
	}
}
