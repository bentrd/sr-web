import { useEffect, useRef, useState, type FormEvent } from "react";
import { useApp } from "../state/AppState";
import { rgbToCss } from "./color";

interface ChatPanelProps {
	// Compact variant for the in-game overlay; lobby uses the default.
	variant?: "lobby" | "game";
}

export function ChatPanel({ variant = "lobby" }: ChatPanelProps): JSX.Element {
	const { chat, sendChat } = useApp();
	const [draft, setDraft] = useState("");
	const [collapsed, setCollapsed] = useState(false);
	const listRef = useRef<HTMLDivElement | null>(null);

	// Stick to the bottom whenever a new message arrives.
	useEffect(() => {
		const el = listRef.current;
		if (el) el.scrollTop = el.scrollHeight;
	}, [chat]);

	function onSubmit(e: FormEvent): void {
		e.preventDefault();
		if (!draft.trim()) return;
		sendChat(draft);
		setDraft("");
	}

	return (
		<aside className={`chat chat-${variant} ${collapsed ? "chat-collapsed" : ""}`}>
			<header className="chat-header">
				<span>Chat</span>
				<button
					type="button"
					className="chat-toggle"
					aria-label={collapsed ? "Expand chat" : "Collapse chat"}
					onClick={() => setCollapsed((c) => !c)}
				>
					{collapsed ? "▲" : "▼"}
				</button>
			</header>
			{!collapsed && (
				<>
					<div className="chat-list" ref={listRef}>
						{chat.length === 0 ? (
							<p className="chat-empty">No messages yet.</p>
						) : (
							chat.map((m, i) => (
								<div
									key={`${m.ts}-${i}`}
									className={`chat-msg chat-${m.kind}`}
								>
									{m.kind === "user" ? (
										<>
											<span
												className="chat-name"
												style={{ color: rgbToCss(m.color) }}
											>
												{m.name}
											</span>
											<span className="chat-text">{m.text}</span>
										</>
									) : (
										<span className="chat-system-text">— {m.text} —</span>
									)}
								</div>
							))
						)}
					</div>
					<form className="chat-form" onSubmit={onSubmit}>
						<input
							type="text"
							className="chat-input"
							value={draft}
							onChange={(e) => setDraft(e.target.value)}
							placeholder="Say something… (Enter)"
							maxLength={240}
							onKeyDown={(e) => {
								// Don't let game keys leak when typing in chat.
								e.stopPropagation();
								if (e.key === "Escape") (e.target as HTMLInputElement).blur();
							}}
						/>
					</form>
				</>
			)}
		</aside>
	);
}
