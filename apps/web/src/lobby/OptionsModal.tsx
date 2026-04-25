import { useEffect } from "react";
import {
	FPS_MAX,
	FPS_MIN,
	useApp,
} from "../state/AppState";
import {
	colorToHex,
	hexToColor,
	HEAD_SIZE_MAX,
	HEAD_SIZE_MIN,
	type ColorRgb,
	type Visuals,
} from "../state/visuals";

interface OptionsModalProps {
	open: boolean;
	onClose: () => void;
}

interface ColorField {
	key: keyof Pick<Visuals, "bg" | "walls" | "grappleStripe" | "wallclimbStripe" | "grappleCord" | "grappleHead">;
	label: string;
	hint: string;
}

// Order matches the user's mental model: scene → world → grapple parts.
const COLOR_FIELDS: readonly ColorField[] = [
	{ key: "bg",              label: "Background",        hint: "Play-area clear color" },
	{ key: "walls",           label: "Walls",             hint: "Tile body" },
	{ key: "grappleStripe",   label: "Grapple",           hint: "Top stripe on grappable ceilings" },
	{ key: "wallclimbStripe", label: "Wallclimb",         hint: "Side stripe on climbable walls" },
	{ key: "grappleCord",     label: "Grapple cord",      hint: "Rope" },
	{ key: "grappleHead",     label: "Grapple head",      hint: "Hook tip rectangle" },
] as const;

export function OptionsModal({ open, onClose }: OptionsModalProps): JSX.Element | null {
	const { visuals, setVisuals, resetVisuals, targetFps, setTargetFps } = useApp();

	useEffect(() => {
		if (!open) return;
		const handler = (e: KeyboardEvent): void => {
			if (e.key === "Escape") onClose();
		};
		window.addEventListener("keydown", handler);
		return () => window.removeEventListener("keydown", handler);
	}, [open, onClose]);

	if (!open) return null;

	function updateColor(key: ColorField["key"], color: ColorRgb): void {
		setVisuals({ ...visuals, [key]: color });
	}

	return (
		<div className="modal-backdrop" onClick={onClose} role="presentation">
			<div
				className="modal modal-wide"
				onClick={(e) => e.stopPropagation()}
				role="dialog"
				aria-label="Options"
			>
				<header className="modal-header">
					<h2>Options</h2>
					<button
						type="button"
						className="link-button"
						onClick={resetVisuals}
					>
						Reset to defaults
					</button>
				</header>

				<table className="bindings-table">
					<tbody>
						{COLOR_FIELDS.map((field) => (
							<tr key={field.key}>
								<td className="bindings-label">
									<div>{field.label}</div>
									<div className="visual-hint">{field.hint}</div>
								</td>
								<td className="visual-color-cell">
									<input
										type="color"
										className="visual-color"
										value={colorToHex(visuals[field.key])}
										onChange={(e) => updateColor(field.key, hexToColor(e.target.value))}
									/>
									<span className="visual-hex">{colorToHex(visuals[field.key])}</span>
								</td>
							</tr>
						))}
					</tbody>
				</table>

				<div className="fps-row">
					<label className="bindings-label" htmlFor="head-size-slider">
						Grapple head size
					</label>
					<input
						id="head-size-slider"
						type="range"
						min={HEAD_SIZE_MIN}
						max={HEAD_SIZE_MAX}
						step={1}
						value={visuals.grappleHeadSize}
						onChange={(e) =>
							setVisuals({ ...visuals, grappleHeadSize: Number(e.target.value) })
						}
					/>
					<span className="fps-value">{Math.round(visuals.grappleHeadSize)}</span>
				</div>

				<div className="fps-row">
					<label className="bindings-label" htmlFor="options-fps-slider">
						Render FPS
					</label>
					<input
						id="options-fps-slider"
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
