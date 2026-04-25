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
