import type { QuickChatConfig, QuickChatSlot } from "../state/quickChat";

interface QuickChatMenuProps {
	activeMenu: number | null;
	quickChat: QuickChatConfig;
	playerColor: string;
}

export function QuickChatMenu({ activeMenu, quickChat, playerColor }: QuickChatMenuProps): JSX.Element | null {
	if (activeMenu === null) return null;

	const items: readonly { key: number; text: string }[] = [1, 2, 3].map((sub) => {
		const slotKey = `${activeMenu}-${sub}` as QuickChatSlot;
		return { key: sub, text: quickChat[slotKey] || "" };
	});

	return (
		<div className="quickchat-menu" role="dialog" aria-label={`Quick chat menu ${activeMenu}`}>
			{items.map((item) => (
				<div key={item.key} className="quickchat-cell">
					<span className="quickchat-key" style={{ backgroundColor: playerColor }}>{item.key}</span>
					<span className="quickchat-text">{item.text}</span>
				</div>
			))}
			<div className="quickchat-cell quickchat-cancel">
				<span className="quickchat-key" style={{ backgroundColor: playerColor }}>4</span>
				<span className="quickchat-text">Cancel</span>
			</div>
		</div>
	);
}