import { useEffect } from "react";
import { useApp } from "../state/AppState";
import { rgbToCss } from "./color";
import { DEFAULT_QUICK_CHAT, type QuickChatSlot } from "../state/quickChat";

interface QuickChatModalProps {
	open: boolean;
	onClose: () => void;
}

export function QuickChatModal({ open, onClose }: QuickChatModalProps): JSX.Element | null {
	const { quickChat, setQuickChat, identity } = useApp();

	useEffect(() => {
		if (!open) return;
		const handler = (e: KeyboardEvent): void => {
			if (e.key === "Escape") onClose();
		};
		window.addEventListener("keydown", handler);
		return () => window.removeEventListener("keydown", handler);
	}, [open, onClose]);

	if (!open) return null;

	const keyColor = rgbToCss(identity.color);

	return (
		<div className="modal-backdrop" onClick={onClose} role="presentation">
			<div
				className="modal modal-wide"
				onClick={(e) => e.stopPropagation()}
				role="dialog"
				aria-label="Quick Chat"
			>
				<header className="modal-header">
					<h2>Quick Chat</h2>
					<button
						type="button"
						className="link-button"
						onClick={() => setQuickChat({ ...DEFAULT_QUICK_CHAT })}
					>
						Reset to defaults
					</button>
				</header>

				{[1, 2, 3, 4].map((menu) => {
					const cells: Array<{
						key: number;
						isCancel: boolean;
						slotKey?: QuickChatSlot;
					}> = [
						{ key: 1, isCancel: false, slotKey: `${menu}-1` as QuickChatSlot },
						{ key: 2, isCancel: false, slotKey: `${menu}-2` as QuickChatSlot },
						{ key: 3, isCancel: false, slotKey: `${menu}-3` as QuickChatSlot },
						{ key: 4, isCancel: true },
					];

					return (
						<div key={menu} className="quickchat-modal-section">
							<h3 className="quickchat-modal-title">Menu {menu} (key {menu})</h3>
							<div className="quickchat-modal-grid">
								{cells.map((cell) => (
									<div
										key={cell.key}
										className={`quickchat-modal-cell${cell.isCancel ? " quickchat-modal-cancel" : ""}`}
									>
										<span
											className="quickchat-modal-key"
											style={{ backgroundColor: keyColor }}
										>
											{cell.key}
										</span>
										{cell.isCancel ? (
											<span className="quickchat-modal-label">Cancel</span>
										) : (
											<input
												type="text"
												className="quickchat-modal-input"
												value={quickChat[cell.slotKey!]}
												onChange={(e) => {
													const next = { ...quickChat };
													next[cell.slotKey!] = e.target.value.slice(0, 40);
													setQuickChat(next);
												}}
												maxLength={40}
											/>
										)}
									</div>
								))}
							</div>
						</div>
					);
				})}

				<footer className="modal-footer">
					<button type="button" onClick={onClose}>
						Done
					</button>
				</footer>
			</div>
		</div>
	);
}