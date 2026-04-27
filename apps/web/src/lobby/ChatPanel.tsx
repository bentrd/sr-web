import { useEffect, useRef, useState, type FormEvent } from "react";
import type { PlayerInfo } from "@sr-web/protocol";
import { useApp } from "../state/AppState";
import { rgbToCss } from "./color";
import { eventToBinding, FOCUS_CHAT_EVENT } from "../state/bindings";
import { CommandPalette, type ParsedCommand } from "./CommandPalette";

interface ChatPanelProps {
	// Compact variant for the in-game overlay; lobby uses the default.
	variant?: "lobby" | "game";
}

// Emit a system-style chat error directly into the local chat state when
// a slash command can't be resolved (unknown player, ambiguous name, etc.)
function localError(text: string): void {
	window.dispatchEvent(new CustomEvent("sr-chat-local", { detail: text }));
}

// Match `/tp <name>` or `/tp <name1> <name2>` where names may contain spaces
// if quoted. Loose: we let the user type unquoted names if they're unique.
function parseTpArgs(rest: string): string[] {
	const args: string[] = [];
	const re = /"([^"]+)"|(\S+)/g;
	let m: RegExpExecArray | null;
	while ((m = re.exec(rest)) !== null) {
		args.push(m[1] ?? m[2] ?? "");
	}
	return args;
}

function findPlayer(
	players: readonly PlayerInfo[],
	needle: string,
	selfId: string,
): PlayerInfo | "ambiguous" | "not_found" {
	const n = needle.trim().toLowerCase();
	if (n === "me" || n === "self") {
		return players.find((p) => p.id === selfId) ?? "not_found";
	}
	const matches = players.filter((p) => p.name.toLowerCase() === n);
	if (matches.length === 0) {
		// Fall back to prefix match if exact misses.
		const prefix = players.filter((p) => p.name.toLowerCase().startsWith(n));
		if (prefix.length === 1) return prefix[0]!;
		if (prefix.length > 1) return "ambiguous";
		return "not_found";
	}
	if (matches.length > 1) return "ambiguous";
	return matches[0]!;
}

export function ChatPanel({ variant = "lobby" }: ChatPanelProps): JSX.Element {
	const { chat, sendChat, bindings, room, playerId } = useApp();
	const [draft, setDraft] = useState("");
	const [collapsed, setCollapsed] = useState(false);
	const [paletteOpen, setPaletteOpen] = useState(false);
	const [localMsgs, setLocalMsgs] = useState<readonly string[]>([]);
	const listRef = useRef<HTMLDivElement | null>(null);
	const inputRef = useRef<HTMLInputElement | null>(null);

	const [dragOffset, setDragOffset] = useState<{ x: number; y: number }>({ x: 0, y: 0 });
	const dragState = useRef<{ startX: number; startY: number; offsetX: number; offsetY: number } | null>(null);

	const onHeaderMouseDown = (e: React.MouseEvent): void => {
		// Only left button
		if (e.button !== 0) return;
		e.preventDefault();
		dragState.current = {
			startX: e.clientX,
			startY: e.clientY,
			offsetX: dragOffset.x,
			offsetY: dragOffset.y,
		};
		const onMove = (ev: MouseEvent): void => {
			if (!dragState.current) return;
			setDragOffset({
				x: dragState.current.offsetX + (ev.clientX - dragState.current.startX),
				y: dragState.current.offsetY + (ev.clientY - dragState.current.startY),
			});
		};
		const onUp = (): void => {
			dragState.current = null;
			window.removeEventListener("mousemove", onMove);
			window.removeEventListener("mouseup", onUp);
		};
		window.addEventListener("mousemove", onMove);
		window.addEventListener("mouseup", onUp);
	};

	const onHeaderDoubleClick = (): void => {
		// Double-click header to reset position
		setDragOffset({ x: 0, y: 0 });
	};

	// Listen for local error events (slash command failures).
	useEffect(() => {
		const handler = (e: Event): void => {
			const msg = (e as CustomEvent<string>).detail;
			if (!msg) return;
			setLocalMsgs((prev) => [...prev.slice(-19), msg]);
		};
		window.addEventListener("sr-chat-local", handler);
		return () => window.removeEventListener("sr-chat-local", handler);
	}, []);

	// Stick to the bottom whenever a new message arrives or chat is re-expanded.
	useEffect(() => {
		const el = listRef.current;
		if (!el || collapsed) return;
		// Use requestAnimationFrame so the browser has laid out the element
		// after display:none → display:block transition.
		requestAnimationFrame(() => {
			el.scrollTop = el.scrollHeight;
		});
	}, [chat, collapsed]);

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

	// Resolve a /tp command and dispatch it. Returns true if the command
	// was handled (whether or not it succeeded — failures surface as a
	// local chat error). Returns false if it wasn't a recognised slash
	// command and the caller should treat the text as a normal chat line.
	function tryRunSlashCommand(raw: string): boolean {
		const text = raw.trim();
		if (!text.startsWith("/")) return false;
		const space = text.indexOf(" ");
		const cmd = (space === -1 ? text : text.slice(0, space)).slice(1).toLowerCase();
		const rest = space === -1 ? "" : text.slice(space + 1);

		if (cmd !== "tp") {
			localError(`Unknown command: /${cmd}`);
			return true;
		}
		if (!room || !playerId) {
			localError("Not in a room.");
			return true;
		}
		const args = parseTpArgs(rest);
		if (args.length < 1 || args.length > 2) {
			localError("Usage: /tp <player>   or   /tp <player1> <player2>");
			return true;
		}
		const [a, b] = args;
		const targetName = args.length === 1 ? "me" : a!;
		const destName = args.length === 1 ? a! : b!;
		const target = findPlayer(room.players, targetName, playerId);
		if (target === "not_found") {
			localError(`No player named "${targetName}".`);
			return true;
		}
		if (target === "ambiguous") {
			localError(`Multiple players match "${targetName}".`);
			return true;
		}
		const dest = findPlayer(room.players, destName, playerId);
		if (dest === "not_found") {
			localError(`No player named "${destName}".`);
			return true;
		}
		if (dest === "ambiguous") {
			localError(`Multiple players match "${destName}".`);
			return true;
		}
		dispatchTp(target.id, dest.id);
		return true;
	}

	function dispatchTp(targetId: string, destId: string): void {
		window.dispatchEvent(
			new CustomEvent("sr-cmd-tp", { detail: { target: targetId, destId } }),
		);
	}

	function onSubmit(e: FormEvent): void {
		e.preventDefault();
		const text = draft;
		if (!text.trim()) {
			// Empty submit (just hit Enter) closes the chat.
			inputRef.current?.blur();
			return;
		}
		setDraft("");
		if (!tryRunSlashCommand(text)) {
			sendChat(text);
		}
		// Closing chat after each message returns key control to the game.
		inputRef.current?.blur();
	}

	function handlePaletteSubmit(cmd: ParsedCommand): void {
		if (cmd.type === "tp") dispatchTp(cmd.target.id, cmd.dest.id);
	}

	const players = room?.players ?? [];

	return (
		<>
			<aside
				className={`chat chat-${variant} ${collapsed ? "chat-collapsed" : ""}`}
				style={{ transform: `translate(${dragOffset.x}px, ${dragOffset.y}px)` }}
			>
				<header
					className="chat-header"
					onMouseDown={onHeaderMouseDown}
					onDoubleClick={onHeaderDoubleClick}
					style={{ cursor: "grab", userSelect: "none" }}
					title="Drag to move · Double-click to reset"
				>
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
				<div style={{ display: collapsed ? "none" : undefined }}>
					<div className="chat-list" ref={listRef}>
						{chat.length === 0 && localMsgs.length === 0 ? (
							<p className="chat-empty">No messages yet.</p>
						) : (
							<>
								{chat.map((m, i) => (
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
								))}
								{localMsgs.map((m, i) => (
									<div key={`local-${i}`} className="chat-msg chat-local">
										<span className="chat-system-text">! {m}</span>
									</div>
								))}
							</>
						)}
					</div>
					<form className="chat-form" onSubmit={onSubmit}>
						<div className="chat-input-row">
							<button
								type="button"
								className="chat-cmd-btn"
								title="Commands (/)"
								aria-label="Open command palette"
								onClick={() => {
									setPaletteOpen(true);
									inputRef.current?.blur();
								}}
							>
								/
							</button>
							<input
								ref={inputRef}
								type="text"
								className="chat-input"
								value={draft}
								onChange={(e) => setDraft(e.target.value)}
								placeholder={`Say something or /tp… (${bindings.chat.label})`}
								maxLength={240}
								onKeyDown={(e) => {
									// Don't let game keys leak when typing in chat.
									e.stopPropagation();
									if (e.key === "Escape") (e.target as HTMLInputElement).blur();
								}}
							/>
						</div>
					</form>
				</div>
			</aside>
			<CommandPalette
				open={paletteOpen}
				players={players}
				selfId={playerId ?? ""}
				onClose={() => setPaletteOpen(false)}
				onSubmit={handlePaletteSubmit}
			/>
		</>
	);
}
