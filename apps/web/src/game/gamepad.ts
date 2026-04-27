// Browser Gamepad API polling for in-game input and rebinding.
//
// Two modes:
// 1. pollGamepads() — called each rAF from Game.tsx. Reads the current
//    gamepad bindings and pushes held state to WASM via sr_push_controller_input.
// 2. pollGamepadForRebind() — called on a short interval from ControlsModal
//    during rebinding capture. Uses edge detection to return the next newly
//    pressed button or flicked axis.
//
// The "standard" gamepad mapping gives consistent indices across controllers.
// https://w3c.github.io/gamepad/#remapping

import type { GamepadBinding, GamepadBindings } from "../state/bindings";
import { getGamepadBindings } from "../state/bindings";

// Dead zone: axis values with magnitude below this are treated as zero.
const DEAD_ZONE = 0.15;

// Threshold for analog → digital conversion (buttons with analog value,
// triggers, and axes all use this).
const THRESHOLD = 0.5;

// Log gamepad connections for debugging non-standard controllers.
// The 8BitDo Pro 2 and similar controllers often report mapping="" instead
// of "standard", which means button indices don't match the Xbox layout.
if (typeof window !== "undefined") {
	window.addEventListener("gamepadconnected", (e) => {
		const gp = e.gamepad;
		console.log(
			"[gamepad] connected:",
			gp.id,
			"| mapping:", JSON.stringify(gp.mapping),
			"| buttons:", gp.buttons.length,
			"| axes:", gp.axes.length,
		);
	});
	window.addEventListener("gamepaddisconnected", (e) => {
		console.log("[gamepad] disconnected:", e.gamepad.id);
	});
}

// --- Button / axis detection helpers ---

function isPressed(gamepad: Gamepad, index: number): boolean {
	if (index >= gamepad.buttons.length) return false;
	const btn = gamepad.buttons[index];
	if (!btn) return false;
	return btn.pressed || btn.value > THRESHOLD;
}

function axisActive(gamepad: Gamepad, index: number, dir: "neg" | "pos"): boolean {
	if (index >= gamepad.axes.length) return false;
	const val = gamepad.axes[index];
	if (val == null) return false;
	if (Math.abs(val) < DEAD_ZONE) return false;
	if (dir === "neg") return val < -THRESHOLD;
	return val > THRESHOLD;
}

// --- Label helpers for display in the controls modal ---

const BUTTON_LABELS: Record<number, string> = {
	0: "A", 1: "B", 2: "X", 3: "Y",
	4: "LB", 5: "RB", 6: "LT", 7: "RT",
	8: "View", 9: "Menu", 10: "L3", 11: "R3",
	12: "\u2191 D-Pad", 13: "\u2193 D-Pad", 14: "\u2190 D-Pad", 15: "\u2192 D-Pad",
};

const AXIS_LABELS: Record<number, Record<string, string>> = {
	0: { neg: "\u2190 Stick", pos: "\u2192 Stick" },
	1: { neg: "\u2191 Stick", pos: "\u2193 Stick" },
	2: { neg: "\u2190 R-Stick", pos: "\u2192 R-Stick" },
	3: { neg: "\u2191 R-Stick", pos: "\u2193 R-Stick" },
};

export function gamepadBindingLabel(gb: GamepadBinding): string {
	if (gb.type === "button") return BUTTON_LABELS[gb.index] ?? `Btn ${gb.index}`;
	return AXIS_LABELS[gb.index]?.[gb.type === "axis_neg" ? "neg" : "pos"]
		?? `${gb.type === "axis_neg" ? "\u2190" : "\u2192"} Axis ${gb.index}`;
}

// --- Per-frame polling (in-game) ---

export type PushControllerInputFn = (action: number, pressed: number) => void;

export function pollGamepads(push: PushControllerInputFn): void {
	const gamepads = navigator.getGamepads();
	// Prefer a standard-mapped gamepad (Xbox layout), but accept any
	// connected controller as fallback. This handles 8BitDo and other
	// controllers that report mapping="" in non-X-input modes.
	const gp = gamepads.find((g) => g !== null && g.mapping === "standard")
		?? gamepads.find((g) => g !== null);
	const bindings = getGamepadBindings();

	// If no controller connected, clear all controller inputs.
	// This handles hot-unplug: all bits go to 0.
	if (!gp) {
		for (let i = 0; i < 8; i++) push(i, 0);
		return;
	}

	const actionKeys = ["left", "right", "jump", "grapple", "slide", "boost", "item", "swap"] as const;
	for (let i = 0; i < actionKeys.length; i++) {
		const gb = bindings[actionKeys[i] as keyof GamepadBindings];
		let pressed = false;
		if (gb) {
			if (gb.type === "button") {
				pressed = isPressed(gp, gb.index);
			} else {
				pressed = axisActive(gp, gb.index, gb.type === "axis_neg" ? "neg" : "pos");
			}
		}
		push(i, pressed ? 1 : 0);
	}
}

// --- Rebind edge detection (controls modal) ---

let rebindPrevButtons: boolean[] = [];
let rebindPrevAxes: number[] = [];

// Copy current gamepad state into the prev-state arrays so held
// buttons / axes don't re-trigger on the next poll.
function snapshotPrevState(gp: Gamepad): void {
	rebindPrevButtons = gp.buttons.map((_b, idx) => isPressed(gp, idx));
	rebindPrevAxes = [...gp.axes];
}

// Reset rebind state to match the current controller state (if any).
// Called when a new capture session starts so already-held buttons
// are not treated as new presses.
export function resetRebindState(): void {
	const gamepads = navigator.getGamepads();
	const gp = gamepads.find((g) => g !== null);
	if (gp) {
		snapshotPrevState(gp);
	} else {
		rebindPrevButtons = [];
		rebindPrevAxes = [];
	}
}

export function pollGamepadForRebind(): GamepadBinding | null {
	const gamepads = navigator.getGamepads();
	// Same priority: standard mapping first, then any controller.
	const gp = gamepads.find((g) => g !== null && g.mapping === "standard")
		?? gamepads.find((g) => g !== null);
	if (!gp) return null;

	// Lazy init on first call: capture current state so we only
	// detect NEW presses, not already-held buttons.
	if (rebindPrevButtons.length === 0 && rebindPrevAxes.length === 0) {
		snapshotPrevState(gp);
	}

	// Check each button for a new press (edge: was released, now pressed)
	for (let i = 0; i < gp.buttons.length; i++) {
		const now = isPressed(gp, i);
		const prev = i < rebindPrevButtons.length ? rebindPrevButtons[i] : false;
		if (now && !prev) {
			snapshotPrevState(gp);
			return {
				type: "button",
				index: i,
				label: BUTTON_LABELS[i] ?? `Btn ${i}`,
			};
		}
	}

	// Check each axis for new movement (edge: was centered, now beyond threshold)
	for (let i = 0; i < gp.axes.length; i++) {
		const val = gp.axes[i];
		if (val == null) continue;
		const prev = i < rebindPrevAxes.length ? (rebindPrevAxes[i] ?? 0) : 0;
		const wasInactive = Math.abs(prev) < THRESHOLD;
		if (wasInactive && Math.abs(val) >= THRESHOLD) {
			const dir = val > 0 ? "pos" : "neg";
			snapshotPrevState(gp);
			return {
				type: dir === "neg" ? "axis_neg" : "axis_pos",
				index: i,
				label: AXIS_LABELS[i]?.[dir]
					?? (dir === "neg" ? "\u2190 Axis " : "\u2192 Axis ") + i,
			};
		}
	}

	// Update previous state for next call
	snapshotPrevState(gp);

	return null;
}