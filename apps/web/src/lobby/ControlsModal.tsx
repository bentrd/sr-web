import { useEffect, useState } from "react";
import { FPS_MAX, FPS_MIN, useApp } from "../state/AppState";
import {
	ACTIONS,
	ACTION_LABELS,
	DEFAULT_BINDINGS,
	type Action,
	eventToBinding,
} from "../state/bindings";

interface ControlsModalProps {
	open: boolean;
	onClose: () => void;
}

export function ControlsModal({ open, onClose }: ControlsModalProps): JSX.Element | null {
	const { bindings, setBindings, targetFps, setTargetFps } = useApp();
	const [capturing, setCapturing] = useState<Action | null>(null);

	// While capturing a rebind, swallow the next keydown and assign it.
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
			setCapturing(null);
		};
		// Capture phase so the canvas focus handlers don't eat it first.
		window.addEventListener("keydown", handler, true);
		return () => window.removeEventListener("keydown", handler, true);
	}, [open, capturing, bindings, setBindings]);

	// Close on backdrop Escape when not capturing.
	useEffect(() => {
		if (!open || capturing) return;
		const handler = (e: KeyboardEvent): void => {
			if (e.key === "Escape") onClose();
		};
		window.addEventListener("keydown", handler);
		return () => window.removeEventListener("keydown", handler);
	}, [open, capturing, onClose]);

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
					<button
						type="button"
						className="link-button"
						onClick={() => setBindings({ ...DEFAULT_BINDINGS })}
					>
						Reset to defaults
					</button>
				</header>
				<table className="bindings-table">
					<tbody>
						{ACTIONS.map((action) => (
							<tr key={action}>
								<td className="bindings-label">{ACTION_LABELS[action]}</td>
								<td>
									<button
										type="button"
										className={`key-cap ${capturing === action ? "key-cap-active" : ""}`}
										onClick={() => setCapturing(action)}
									>
										{capturing === action
											? "press a key…"
											: bindings[action].label}
									</button>
								</td>
							</tr>
						))}
					</tbody>
				</table>

				<div className="fps-row">
					<label className="bindings-label" htmlFor="fps-slider">
						Render FPS
					</label>
					<input
						id="fps-slider"
						type="range"
						min={FPS_MIN}
						max={FPS_MAX}
						step={5}
						value={targetFps}
						onChange={(e) => setTargetFps(Number(e.target.value))}
					/>
					<span className="fps-value">{targetFps}</span>
				</div>

				<footer className="modal-footer">
					<button type="button" onClick={onClose}>
						Done
					</button>
				</footer>
			</div>
		</div>
	);
}
