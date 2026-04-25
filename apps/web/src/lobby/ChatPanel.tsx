import { useEffect, useRef, useState, type FormEvent } from "react";
import { useApp } from "../state/AppState";
import { rgbToCss } from "./color";
import { eventToBinding, FOCUS_CHAT_EVENT } from "../state/bindings";

interface ChatPanelProps {
	// Compact variant for the in-game overlay; lobby uses the default.
	variant?: "lobby" | "game";
}

export function ChatPanel({ variant = "lobby" }: ChatPanelProps): JSX.Element {
	const { chat, sendChat, bindings } = useApp();
	const [draft, setDraft] = useState("");
	const [collapsed, setCollapsed] = useState(false);
	const listRef = useRef<HTMLDivElement | null>(null);
	const inputRef = useRef<HTMLInputElement | null>(null);

	// Stick to the bottom whenever a new message arrives.
	useEffect(() => {
		const el = listRef.current;
		if (el) el.scrollTop = el.scrollHeight;
	}, [chat]);

	// Listen for the user-bound chat key (default Enter). When pressed
	// while focus is anywhere except the chat input, we steal focus AND
	// kill the event so the keystroke that opened chat doesn't ALSO get
	// inserted into the input or seen by WASM as a game key.
	useEffect(() => {
		const chatCode = bindings.chat.code;
		const onKey = (e: KeyboardEvent): void => {
			if (document.activeElement === inputRef.current) return;
			const b = eventToBinding(e);
			if (b === null || b.code !== chatCode) return;
			e.preventDefault();
			e.stopImmediatePropagation();
			// Defer focus by a microtask: lets the current event finish
			// dispatching before the input becomes the active element.
			queueMicrotask(() => inputRef.current?.focus());
			setCollapsed(false);
		};
		// React to a programmatic focus request too (future-proofing —
		// e.g. a "/chat" button or slash-command).
		const onFocusReq = (): void => {
			setCollapsed(false);
			inputRef.current?.focus();
		};
		window.addEventListener("keydown", onKey, true);
		window.addEventListener(FOCUS_CHAT_EVENT, onFocusReq);
		return () => {
			window.removeEventListener("keydown", onKey, true);
			window.removeEventListener(FOCUS_CHAT_EVENT, onFocusReq);
		};
	}, [bindings.chat.code]);

	function onSubmit(e: FormEvent): void {
		e.preventDefault();
		if (!draft.trim()) {
			// Empty submit (just hit Enter) closes the chat.
			inputRef.current?.blur();
			return;
		}
		sendChat(draft);
		setDraft("");
		// Closing chat after each message returns key control to the game.
		inputRef.current?.blur();
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
							ref={inputRef}
							type="text"
							className="chat-input"
							value={draft}
							onChange={(e) => setDraft(e.target.value)}
							placeholder={`Say something… (${bindings.chat.label})`}
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
