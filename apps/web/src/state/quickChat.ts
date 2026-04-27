const QUICK_CHAT_KEY = "sr-web.quickchat";

export type QuickChatSlot =
	| "1-1" | "1-2" | "1-3"
	| "2-1" | "2-2" | "2-3"
	| "3-1" | "3-2" | "3-3"
	| "4-1" | "4-2" | "4-3";

export type QuickChatConfig = Record<QuickChatSlot, string>;

export const DEFAULT_QUICK_CHAT: QuickChatConfig = {
	"1-1": "Wow!",
	"1-2": "Well played!",
	"1-3": "Nice move!",
	"2-1": "Good luck!",
	"2-2": "Good game!",
	"2-3": "Goodbye!",
	"3-1": "Thanks!",
	"3-2": "Haha!",
	"3-3": "Grrr!",
	"4-1": "Oops!",
	"4-2": "Sorry!",
	"4-3": "Nooo!",
};

const QUICK_CHAT_SLOTS: readonly QuickChatSlot[] = [
	"1-1", "1-2", "1-3",
	"2-1", "2-2", "2-3",
	"3-1", "3-2", "3-3",
	"4-1", "4-2", "4-3",
];

const MAX_LEN = 40;

export function loadQuickChat(): QuickChatConfig {
	try {
		const raw = localStorage.getItem(QUICK_CHAT_KEY);
		if (!raw) return { ...DEFAULT_QUICK_CHAT };
		const parsed: unknown = JSON.parse(raw);
		if (typeof parsed !== "object" || parsed === null) return { ...DEFAULT_QUICK_CHAT };
		const out = { ...DEFAULT_QUICK_CHAT };
		const rec = parsed as Record<string, unknown>;
		for (const slot of QUICK_CHAT_SLOTS) {
			if (typeof rec[slot] === "string" && (rec[slot] as string).trim().length > 0) {
				out[slot] = String(rec[slot]).slice(0, MAX_LEN);
			}
		}
		return out;
	} catch {
		return { ...DEFAULT_QUICK_CHAT };
	}
}

export function saveQuickChat(c: QuickChatConfig): void {
	localStorage.setItem(QUICK_CHAT_KEY, JSON.stringify(c));
}