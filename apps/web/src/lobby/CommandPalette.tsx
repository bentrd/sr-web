import { useState } from "react";
import type { PlayerInfo } from "@sr-web/protocol";
import { rgbToCss } from "./color";

export interface ParsedCommand {
	type: "tp";
	target: PlayerInfo;   // who moves
	dest: PlayerInfo;     // where they go (their world position is captured later)
}

interface CommandPaletteProps {
	open: boolean;
	players: readonly PlayerInfo[];
	selfId: string;
	onClose: () => void;
	onSubmit: (cmd: ParsedCommand) => void;
}

// Two-step picker for /tp. Step 1 picks the player who should move
// (defaults to "yourself"). Step 2 picks the destination player.
// Closing the palette without picking is a no-op.
export function CommandPalette({
	open,
	players,
	selfId,
	onClose,
	onSubmit,
}: CommandPaletteProps): JSX.Element | null {
	const [target, setTarget] = useState<PlayerInfo | null>(null);

	if (!open) return null;

	const self = players.find((p) => p.id === selfId) ?? null;
	const step = target === null ? "target" : "dest";

	const reset = (): void => setTarget(null);

	function pickTarget(p: PlayerInfo): void {
		setTarget(p);
	}

	function pickDest(p: PlayerInfo): void {
		if (!target) return;
		onSubmit({ type: "tp", target, dest: p });
		reset();
		onClose();
	}

	function handleClose(): void {
		reset();
		onClose();
	}

	const showSelfShortcut = step === "target" && self !== null;

	return (
		<div className="modal-backdrop" onClick={handleClose} role="presentation">
			<div
				className="modal cmd-palette"
				onClick={(e) => e.stopPropagation()}
				role="dialog"
				aria-label="Commands"
			>
				<header className="modal-header">
					<h2>{step === "target" ? "/tp — who?" : `/tp ${target?.name} → who?`}</h2>
					<button type="button" className="link-button" onClick={handleClose}>
						Cancel
					</button>
				</header>

				{showSelfShortcut && self && (
					<button
						type="button"
						className="cmd-self"
						onClick={() => pickTarget(self)}
					>
						Teleport <strong>yourself</strong> to a player
					</button>
				)}

				<ul className="cmd-player-list">
					{players.map((p) => {
						const onClick = step === "target" ? pickTarget : pickDest;
						const isSelf = p.id === selfId;
						return (
							<li key={p.id}>
								<button
									type="button"
									className="cmd-player-row"
									onClick={() => onClick(p)}
								>
									<span
										className="swatch"
										style={{ backgroundColor: rgbToCss(p.color) }}
									/>
									<span className="player-name">{p.name}</span>
									{isSelf && <span className="you-tag">you</span>}
								</button>
							</li>
						);
					})}
				</ul>

				{step === "dest" && (
					<footer className="modal-footer">
						<button type="button" onClick={reset}>
							← Back
						</button>
					</footer>
				)}
			</div>
		</div>
	);
}
