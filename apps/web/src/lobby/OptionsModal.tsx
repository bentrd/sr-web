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
	type ColorRgba,
	type SpeedometerMode,
	type Visuals,
} from "../state/visuals";

interface OptionsModalProps {
	open: boolean;
	onClose: () => void;
}

type RgbKey = keyof Pick<Visuals, "bg" | "walls" | "grappleStripe" | "wallclimbStripe" | "grappleCord" | "grappleHead">;
type RgbaKey = keyof Pick<Visuals, "boostSection" | "boostPickup">;

interface RgbField { kind: "rgb"; key: RgbKey; label: string; hint: string }
interface RgbaField { kind: "rgba"; key: RgbaKey; label: string; hint: string }
type ColorField = RgbField | RgbaField;

// Order matches the user's mental model: scene → world → grapple → boost.
const COLOR_FIELDS: readonly ColorField[] = [
	{ kind: "rgb",  key: "bg",              label: "Background",        hint: "Play-area clear color" },
	{ kind: "rgb",  key: "walls",           label: "Walls",             hint: "Tile body" },
	{ kind: "rgb",  key: "grappleStripe",   label: "Grapple",           hint: "Grappable ceiling stripe" },
	{ kind: "rgb",  key: "wallclimbStripe", label: "Wallclimb",         hint: "Climbable wall stripe" },
	{ kind: "rgb",  key: "grappleCord",     label: "Grapple cord",      hint: "Rope" },
	{ kind: "rgb",  key: "grappleHead",     label: "Grapple head",      hint: "Hook tip rectangle" },
	{ kind: "rgba", key: "boostSection",    label: "Boost pickup",      hint: "Tinted super-boost volume" },
	{ kind: "rgba", key: "boostPickup",     label: "Boost section",     hint: "Speed-boost strip" },
] as const;

const SPEEDOMETER_OPTIONS: readonly { value: SpeedometerMode; label: string; hint: string }[] = [
	{ value: "off", label: "Off", hint: "Hidden" },
	{ value: "on",  label: "On",  hint: "Bottom-left readout" },
];

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

	function updateRgb(key: RgbKey, color: ColorRgb): void {
		setVisuals({ ...visuals, [key]: color });
	}

	function updateRgba(key: RgbaKey, color: ColorRgba): void {
		setVisuals({ ...visuals, [key]: color });
	}

	function setSpeedometer(mode: SpeedometerMode): void {
		setVisuals({ ...visuals, speedometer: mode });
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

				<div className="visual-grid">
					{COLOR_FIELDS.map((field) => {
						const value = visuals[field.key];
						const rgb: ColorRgb =
							field.kind === "rgba"
								? [value[0], value[1], value[2]]
								: (value as ColorRgb);
						const alpha = field.kind === "rgba" ? (value as ColorRgba)[3] : 1;
						return (
							<div key={field.key} className="visual-row">
								<div className="visual-row-label">
									<div>{field.label}</div>
									<div className="visual-hint">{field.hint}</div>
								</div>
								<input
									type="color"
									className="visual-color"
									value={colorToHex(rgb)}
									onChange={(e) => {
										const next = hexToColor(e.target.value);
										if (field.kind === "rgba") {
											updateRgba(field.key, [next[0], next[1], next[2], alpha]);
										} else {
											updateRgb(field.key, next);
										}
									}}
								/>
								{field.kind === "rgba" && (
									<input
										type="range"
										className="visual-alpha"
										min={0}
										max={1}
										step={0.05}
										value={alpha}
										aria-label={`${field.label} alpha`}
										onChange={(e) =>
											updateRgba(field.key, [rgb[0], rgb[1], rgb[2], Number(e.target.value)])
										}
									/>
								)}
							</div>
						);
					})}
				</div>

				<div className="visual-section">
					<div className="visual-row-label">
						<div>Speedometer</div>
						<div className="visual-hint">√(vx² + vy²) — bottom-left readout</div>
					</div>
					<div role="radiogroup" aria-label="Speedometer mode" className="seg-toggle">
						{SPEEDOMETER_OPTIONS.map((opt) => (
							<button
								key={opt.value}
								type="button"
								role="radio"
								aria-checked={visuals.speedometer === opt.value}
								onClick={() => setSpeedometer(opt.value)}
								className={`seg-toggle-btn ${
									visuals.speedometer === opt.value ? "seg-toggle-active" : ""
								}`}
								title={opt.hint}
							>
								{opt.label}
							</button>
						))}
					</div>
				</div>

				<div className="visual-section">
					<div className="visual-row-label">
						<div>Show other players' trails</div>
						<div className="visual-hint">Renders peers' .srt trails at half opacity</div>
					</div>
					<div role="radiogroup" aria-label="Show other players' trails" className="seg-toggle">
						<button
							type="button"
							role="radio"
							aria-checked={!visuals.showGhostTrails}
							onClick={() => setVisuals({ ...visuals, showGhostTrails: false })}
							className={`seg-toggle-btn ${
								!visuals.showGhostTrails ? "seg-toggle-active" : ""
							}`}
							title="Hide all peer trails"
						>
							Off
						</button>
						<button
							type="button"
							role="radio"
							aria-checked={visuals.showGhostTrails}
							onClick={() => setVisuals({ ...visuals, showGhostTrails: true })}
							className={`seg-toggle-btn ${
								visuals.showGhostTrails ? "seg-toggle-active" : ""
							}`}
							title="Render peer trails (default)"
						>
							On
						</button>
					</div>
				</div>

				<div className="visual-section">
					<div className="visual-row-label">
						<div>RG corridor grid</div>
						<div className="visual-hint">Subtle 16 wu reference grid in the RG challenge corridor</div>
					</div>
					<div role="radiogroup" aria-label="RG corridor grid" className="seg-toggle">
						<button
							type="button"
							role="radio"
							aria-checked={!visuals.showRgGrid}
							onClick={() => setVisuals({ ...visuals, showRgGrid: false })}
							className={`seg-toggle-btn ${
								!visuals.showRgGrid ? "seg-toggle-active" : ""
							}`}
							title="Hide grid (default)"
						>
							Off
						</button>
						<button
							type="button"
							role="radio"
							aria-checked={visuals.showRgGrid}
							onClick={() => setVisuals({ ...visuals, showRgGrid: true })}
							className={`seg-toggle-btn ${
								visuals.showRgGrid ? "seg-toggle-active" : ""
							}`}
							title="Show subtle 16 wu grid behind the corridor"
						>
							On
						</button>
					</div>
				</div>

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
						Engine FPS
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
