import { useCallback, useEffect, useRef, useState } from "react";
import { useApp } from "../state/AppState";
import {
	ACTIONS,
	ACTION_LABELS,
	DEFAULT_BINDINGS,
	DEFAULT_GAMEPAD_BINDINGS,
	type Action,
	type GamepadBinding,
	eventToBinding,
	getGamepadBindings,
	setGamepadBindings,
} from "../state/bindings";
import { gamepadBindingLabel, pollGamepadForRebind, resetRebindState } from "../game/gamepad";

interface ControlsModalProps {
	open: boolean;
	onClose: () => void;
}

// Advance to the next action after a successful rebind. Returns null
// when we're at the last action (stops the autotab chain).
function nextAction(current: Action): Action | null {
	const idx = ACTIONS.indexOf(current);
	if (idx < 0 || idx >= ACTIONS.length - 1) return null;
	return ACTIONS[idx + 1]!;
}

export function ControlsModal({ open, onClose }: ControlsModalProps): JSX.Element | null {
	const { bindings, setBindings } = useApp();
	const [capturing, setCapturing] = useState<Action | null>(null);
	const [capturingGp, setCapturingGp] = useState<Action | null>(null);
	const [gpBindings, setGpBindings] = useState<Record<string, GamepadBinding | null>>(() => {
		const saved = getGamepadBindings();
		const out: Record<string, GamepadBinding | null> = {};
		for (const a of ACTIONS) out[a] = saved[a] ?? null;
		return out;
	});

	const gpIntervalRef = useRef<ReturnType<typeof setInterval> | null>(null);

	// Persist gamepad bindings whenever they change.
	const updateGpBindings = useCallback((next: Record<string, GamepadBinding | null>) => {
		setGpBindings(next);
		const clean: Record<string, GamepadBinding> = {};
		for (const a of ACTIONS) {
			if (next[a]) clean[a] = next[a]!;
		}
		setGamepadBindings(clean);
	}, []);

	// While capturing a keyboard rebind, swallow the next keydown.
	useEffect(() => {
		if (!open || !capturing) return;
		const handler = (e: KeyboardEvent): void => {
			if (e.key === "Escape") {
				setCapturing(null);
				e.preventDefault();
				return;
			}
			const binding = eventToBinding(e);
			if (binding === null) return;
			e.preventDefault();
			e.stopPropagation();
			// Reject duplicate assignments — swap with whoever owns it.
			const conflict = ACTIONS.find(
				(a) => a !== capturing && bindings[a].code === binding.code,
			);
			const next = { ...bindings };
			if (conflict) next[conflict] = bindings[capturing];
			next[capturing] = binding;
			setBindings(next);
			setCapturing(nextAction(capturing));
		};
		window.addEventListener("keydown", handler, true);
		return () => window.removeEventListener("keydown", handler, true);
	}, [open, capturing, bindings, setBindings]);

	// While capturing a gamepad rebind, poll for the next button/axis press.
	useEffect(() => {
		if (!open || !capturingGp) {
			if (gpIntervalRef.current) {
				clearInterval(gpIntervalRef.current);
				gpIntervalRef.current = null;
			}
			resetRebindState();
			return;
		}
		resetRebindState();
		gpIntervalRef.current = setInterval(() => {
			const gb = pollGamepadForRebind();
			if (gb) {
				const next = { ...gpBindings };
				// Swap if another action already uses this binding.
				const conflict = ACTIONS.find((a) => {
					if (a === capturingGp) return false;
					const existing = next[a];
					if (!existing) return false;
					return existing.type === gb.type && existing.index === gb.index;
				});
				if (conflict) {
					const swap = next[capturingGp];
					if (swap) next[conflict] = swap;
				}
				next[capturingGp] = gb;
				updateGpBindings(next);
				setCapturingGp(nextAction(capturingGp));
			}
		}, 16); // ~60Hz polling during capture
		return () => {
			if (gpIntervalRef.current) {
				clearInterval(gpIntervalRef.current);
				gpIntervalRef.current = null;
			}
		};
	}, [open, capturingGp, gpBindings, updateGpBindings]);

	// Close on backdrop Escape when not capturing.
	useEffect(() => {
		if (!open || capturing || capturingGp) return;
		const handler = (e: KeyboardEvent): void => {
			if (e.key === "Escape") onClose();
		};
		window.addEventListener("keydown", handler);
		return () => window.removeEventListener("keydown", handler);
	}, [open, capturing, capturingGp, onClose]);

	if (!open) return null;

	return (
		<div className="modal-backdrop" onClick={onClose} role="presentation">
			<div
				className="modal"
				onClick={(e) => e.stopPropagation()}
				role="dialog"
				aria-label="Controls"
			>
				<header className="modal-header">
					<h2>Controls</h2>
					<div style={{ display: "flex", gap: 8 }}>
						<button
							type="button"
							className="link-button"
							onClick={() => setBindings({ ...DEFAULT_BINDINGS })}
						>
							Reset keys
						</button>
						<button
							type="button"
							className="link-button"
							onClick={() => updateGpBindings(
								Object.fromEntries(ACTIONS.map((a) => [a, DEFAULT_GAMEPAD_BINDINGS[a] ?? null]))
							)}
						>
							Reset gamepad
						</button>
					</div>
				</header>
				<table className="bindings-table">
					<thead>
						<tr>
							<th className="bindings-label">Action</th>
							<th>Keyboard</th>
							<th>Gamepad</th>
						</tr>
					</thead>
					<tbody>
						{ACTIONS.map((action) => (
							<tr key={action}>
								<td className="bindings-label">{ACTION_LABELS[action]}</td>
								<td>
									<button
										type="button"
										className={`key-cap ${capturing === action ? "key-cap-active" : ""}`}
										onClick={() => { setCapturing(action); setCapturingGp(null); }}
										disabled={capturingGp !== null}
									>
										{capturing === action
											? "press a key\u2026"
											: bindings[action].label}
									</button>
								</td>
								<td>
									<button
										type="button"
										className={`key-cap ${capturingGp === action ? "key-cap-active" : ""}`}
										onClick={() => { setCapturingGp(action); setCapturing(null); }}
										disabled={capturing !== null}
									>
										{capturingGp === action
											? "press a button\u2026"
											: gpBindings[action]
												? gamepadBindingLabel(gpBindings[action]!)
												: "\u2014"}
									</button>
								</td>
							</tr>
						))}
					</tbody>
				</table>

				<footer className="modal-footer">
					<button type="button" onClick={onClose}>
						Done
					</button>
				</footer>
			</div>
		</div>
	);
}