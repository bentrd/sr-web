// User-rebindable controls. Stored in localStorage as a partial map
// (only entries that differ from defaults need to be persisted, but we
// just write the whole thing — it's 8 small numbers).
//
// On the wire to WASM these are GLFW key codes. To keep the modal in sync
// with what Emscripten's GLFW polls at runtime, we mirror its translation
// path: browser KeyboardEvent.keyCode → DOMToGLFWKeyCode (the same table
// baked into sr.js). Capturing via event.code instead would mismatch on
// non-US layouts (AZERTY-Q reports event.code="KeyA" but keyCode=81 on
// macOS Chrome — Emscripten sees 81, so we have to too).

// Game actions are forwarded to WASM via sr_set_binding(idx, glfwKey).
// Their order MUST match the emu::input enum on the C++ side.
export const GAME_ACTIONS = [
	"left",
	"right",
	"jump",
	"grapple",
	"slide",
	"boost",
	"item",
	"swap",
] as const;

// UI actions are handled in JS only (never pushed to WASM).
export const UI_ACTIONS = ["chat", "reset"] as const;

export const ACTIONS = [...GAME_ACTIONS, ...UI_ACTIONS] as const;
export type Action = typeof ACTIONS[number];

export type Binding = { code: number; label: string };
export type Bindings = Record<Action, Binding>;

export const DEFAULT_BINDINGS: Bindings = {
	left:    { code: 65,  label: "A" },     // GLFW_KEY_A
	right:   { code: 68,  label: "D" },     // GLFW_KEY_D
	jump:    { code: 32,  label: "Space" }, // GLFW_KEY_SPACE
	grapple: { code: 87,  label: "W" },     // GLFW_KEY_W
	slide:   { code: 83,  label: "S" },     // GLFW_KEY_S
	boost:   { code: 340, label: "Shift" }, // GLFW_KEY_LEFT_SHIFT
	item:    { code: 69,  label: "E" },     // GLFW_KEY_E
	swap:    { code: 70,  label: "F" },     // GLFW_KEY_F
	chat:    { code: 257, label: "Enter" }, // GLFW_KEY_ENTER (UI-only)
	reset:   { code: 82,  label: "R" },     // GLFW_KEY_R       (UI-only)
};

const STORAGE_KEY = "sr-web.bindings";

export function loadBindings(): Bindings {
	try {
		const raw = localStorage.getItem(STORAGE_KEY);
		if (!raw) return { ...DEFAULT_BINDINGS };
		const parsed = JSON.parse(raw) as Partial<Record<Action, Partial<Binding>>>;
		const out = { ...DEFAULT_BINDINGS };
		for (const a of ACTIONS) {
			const v = parsed[a];
			if (
				v &&
				typeof v.code === "number" && v.code > 0 && v.code < 1024 &&
				typeof v.label === "string" && v.label.length > 0
			) {
				out[a] = { code: v.code, label: v.label };
			}
		}
		return out;
	} catch {
		return { ...DEFAULT_BINDINGS };
	}
}

export function saveBindings(b: Bindings): void {
	try {
		localStorage.setItem(STORAGE_KEY, JSON.stringify(b));
	} catch {
		// localStorage may be disabled
	}
}

// Mirror of Emscripten's DOMToGLFWKeyCode (library_glfw.js). Letters and
// digits are identity; named keys translate to the GLFW range. Anything
// not listed (browser-specific oddities) returns -1, matching the upstream
// default.
function domKeyCodeToGlfw(keyCode: number): number {
	switch (keyCode) {
		case 32: return 32;
		case 222: return 39;
		case 188: return 44;
		case 173: case 189: return 45;
		case 190: return 46;
		case 191: return 47;
		case 48: case 49: case 50: case 51: case 52:
		case 53: case 54: case 55: case 56: case 57:
			return keyCode;
		case 59: return 59;
		case 61: case 187: return 61;
		case 65: case 66: case 67: case 68: case 69:
		case 70: case 71: case 72: case 73: case 74:
		case 75: case 76: case 77: case 78: case 79:
		case 80: case 81: case 82: case 83: case 84:
		case 85: case 86: case 87: case 88: case 89:
		case 90:
			return keyCode;
		case 219: return 91;
		case 220: return 92;
		case 221: return 93;
		case 192: return 96;
		case 27: return 256;
		case 13: return 257;
		case 9: return 258;
		case 8: return 259;
		case 45: return 260;
		case 46: return 261;
		case 39: return 262;
		case 37: return 263;
		case 40: return 264;
		case 38: return 265;
		case 33: return 266;
		case 34: return 267;
		case 36: return 268;
		case 35: return 269;
		case 20: return 280;
		case 145: return 281;
		case 144: return 282;
		case 44: return 283;
		case 19: return 284;
		case 112: return 290;
		case 113: return 291;
		case 114: return 292;
		case 115: return 293;
		case 116: return 294;
		case 117: return 295;
		case 118: return 296;
		case 119: return 297;
		case 120: return 298;
		case 121: return 299;
		case 122: return 300;
		case 123: return 301;
		case 96: return 320;
		case 97: return 321;
		case 98: return 322;
		case 99: return 323;
		case 100: return 324;
		case 101: return 325;
		case 102: return 326;
		case 103: return 327;
		case 104: return 328;
		case 105: return 329;
		case 110: return 330;
		case 111: return 331;
		case 106: return 332;
		case 109: return 333;
		case 107: return 334;
		case 16: return 340;
		case 17: return 341;
		case 18: return 342;
		case 91: case 224: return 343;
		case 93: return 348;
		default: return -1;
	}
}

export function eventToBinding(e: KeyboardEvent): Binding | null {
	const code = domKeyCodeToGlfw(e.keyCode);
	if (code <= 0) return null;
	// Look up named keys first so Space, Shift, etc. don't fall into the
	// length===1 branch (Space's e.key is " ", which would render as a
	// blank cap in the modal).
	const named = NAMED_KEY_LABEL[e.code];
	if (named) return { code, label: named };
	if (e.key.length === 1) {
		const trimmed = e.key.trim();
		// Defensive: a single whitespace key with no e.code match would
		// still render blank. Force "Space" in that case.
		return { code, label: trimmed.length > 0 ? trimmed.toUpperCase() : "Space" };
	}
	return { code, label: e.key };
}

const NAMED_KEY_LABEL: Readonly<Record<string, string>> = {
	Space: "Space",
	Escape: "Esc", Enter: "Enter", Tab: "Tab", Backspace: "Backspace",
	ArrowRight: "→", ArrowLeft: "←", ArrowDown: "↓", ArrowUp: "↑",
	ShiftLeft: "Shift", ControlLeft: "Ctrl", AltLeft: "Alt", MetaLeft: "Meta",
	ShiftRight: "RShift", ControlRight: "RCtrl", AltRight: "RAlt",
};

export const ACTION_LABELS: Readonly<Record<Action, string>> = {
	left: "Left",
	right: "Right",
	jump: "Jump",
	grapple: "Grapple",
	slide: "Slide",
	boost: "Boost",
	item: "Use item",
	swap: "Swap item",
	chat: "Open chat",
	reset: "Reset to start",
};

// Custom DOM event used to ask the active ChatPanel to focus its input
// when the user presses the chat key while focus is somewhere else (e.g.
// the game canvas). Kept loose-typed because it carries no payload.
export const FOCUS_CHAT_EVENT = "sr-focus-chat";
