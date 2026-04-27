import { Database } from "bun:sqlite";
import { join } from "path";

// Gracefully degraded if the app directory is read-only (e.g. test runs).
// In production this resolves to apps/server/leaderboard.db.
let dbPath: string;
try {
	dbPath = join(import.meta.dir, "..", "leaderboard.db");
} catch {
	dbPath = ":memory:";
}

let db: Database;
try {
	db = new Database(dbPath);
	db.run("PRAGMA journal_mode = WAL");
	db.run("PRAGMA busy_timeout = 5000");
	db.run(`
		CREATE TABLE IF NOT EXISTS scores (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			date TEXT NOT NULL,
			player_name TEXT NOT NULL,
			max_speed REAL NOT NULL,
			timestamp INTEGER NOT NULL
		)
	`);
	db.run(`
		CREATE INDEX IF NOT EXISTS idx_scores_date_speed
		ON scores(date, max_speed DESC)
	`);
	db.run(`
		CREATE INDEX IF NOT EXISTS idx_scores_date_name
		ON scores(date, player_name)
	`);
} catch {
	// Fallback in-memory for read-only environments.
	db = new Database(":memory:");
	db.run(`
		CREATE TABLE IF NOT EXISTS scores (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			date TEXT NOT NULL,
			player_name TEXT NOT NULL,
			max_speed REAL NOT NULL,
			timestamp INTEGER NOT NULL
		)
	`);
}

export interface LeaderboardRow {
	rank: number;
	name: string;
	maxSpeed: number;
}

// Insert a score. The caller is responsible for validation and
// rate-limiting — this function is a dumb sink.
export function submitScore(date: string, playerName: string, maxSpeed: number): void {
	const stmt = db.prepare(
		"INSERT INTO scores (date, player_name, max_speed, timestamp) VALUES (?1, ?2, ?3, ?4)",
	);
	stmt.run(date, playerName, maxSpeed, Date.now());
}

// Returns top N entries for a given date, deduplicated by player
// (only the best score per player counts). Rank is 1-based.
export function getDailyLeaderboard(date: string, limit = 20): LeaderboardRow[] {
	const stmt = db.prepare(`
		SELECT player_name, MAX(max_speed) AS best
		FROM scores
		WHERE date = ?1
		GROUP BY player_name
		ORDER BY best DESC
		LIMIT ?2
	`);
	const rows = stmt.all(date, limit) as Array<{ player_name: string; best: number }>;
	return rows.map((r, i) => ({
		rank: i + 1,
		name: r.player_name,
		maxSpeed: Math.round(r.best),
	}));
}

// Get a single player's best speed today (used to craft the score_ack).
export function dailyBestForPlayer(date: string, playerName: string): number {
	const stmt = db.prepare(`
		SELECT MAX(max_speed) AS best
		FROM scores
		WHERE date = ?1 AND player_name = ?2
	`);
	const row = stmt.get(date, playerName) as { best: number | null } | undefined;
	return row?.best != null ? Math.round(row.best) : 0;
}

// Find the rank of a given player today (1-based, 0 if not ranked).
export function rankForPlayer(date: string, playerName: string): number {
	const stmt = db.prepare(`
		SELECT player_name, MAX(max_speed) AS best
		FROM scores
		WHERE date = ?1
		GROUP BY player_name
		ORDER BY best DESC
	`);
	const rows = stmt.all(date) as Array<{ player_name: string; best: number }>;
	const idx = rows.findIndex((r) => r.player_name === playerName);
	return idx >= 0 ? idx + 1 : 0;
}
