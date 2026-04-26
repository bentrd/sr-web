// Recently-uploaded trails — surfaced in the lobby's TrailMenu dropdown
// so users can flip back to a previous upload without re-picking the
// folder. Each entry mirrors what `Identity.trail` carries (name +
// base64-encoded .srt zip + optional icon data URL) plus a stable id
// and an `addedAt` timestamp for FIFO eviction.
//
// Capped at MAX_SAVED so localStorage doesn't bloat — each .srt is up
// to ~285 KB raw → ~384 KB base64; cap of 8 keeps the worst case
// around ~3 MB which is well inside the 5 MB localStorage budget.

const KEY = "sr-web.saved-trails";
export const MAX_SAVED = 8;

export interface SavedTrail {
	id: string;
	name: string;
	b64: string;
	iconDataUrl?: string;
	addedAt: number;
}

function isSavedTrail(v: unknown): v is SavedTrail {
	if (!v || typeof v !== "object") return false;
	const o = v as Record<string, unknown>;
	return (
		typeof o.id === "string" &&
		typeof o.name === "string" &&
		typeof o.b64 === "string" &&
		typeof o.addedAt === "number" &&
		(o.iconDataUrl === undefined || typeof o.iconDataUrl === "string")
	);
}

export function loadSavedTrails(): SavedTrail[] {
	try {
		const raw = localStorage.getItem(KEY);
		if (!raw) return [];
		const parsed = JSON.parse(raw);
		if (!Array.isArray(parsed)) return [];
		return parsed.filter(isSavedTrail);
	} catch {
		return [];
	}
}

export function saveSavedTrails(list: readonly SavedTrail[]): void {
	try {
		localStorage.setItem(KEY, JSON.stringify(list));
	} catch {
		// localStorage may be disabled or full — non-fatal, the list is
		// best-effort UX history.
	}
}

// Add (or replace by name) a trail in the saved list. Replaces the
// existing entry with the same name so re-uploading the same folder
// doesn't double-list it; otherwise appends and trims oldest first.
export function addSavedTrail(
	current: readonly SavedTrail[],
	entry: Omit<SavedTrail, "id" | "addedAt">,
): SavedTrail[] {
	const id = entry.name + "::" + entry.b64.slice(0, 12);
	const now = Date.now();
	const next: SavedTrail[] = [
		{ id, addedAt: now, ...entry },
		...current.filter((t) => t.name !== entry.name),
	];
	if (next.length > MAX_SAVED) next.length = MAX_SAVED;
	return next;
}

export function removeSavedTrail(
	current: readonly SavedTrail[],
	id: string,
): SavedTrail[] {
	return current.filter((t) => t.id !== id);
}
