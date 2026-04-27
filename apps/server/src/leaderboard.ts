import { Database } from "bun:sqlite";
import { join } from "path";

// On Fly.io, mount a persistent volume at /data/ and set
// LEADERBOARD_DB_PATH=/data/leaderboard.db so scores survive deploys.
// Locally the default keeps the file next to the server source.
let dbPath: string;
const envPath = process.env.LEADERBOARD_DB_PATH;
if (envPath) {
	dbPath = envPath;
} else {
	try {
		dbPath = join(import.meta.dir, "..", "leaderboard.db");
	} catch {
		dbPath = ":memory:";
	}
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

// Returns top N entries all-time, deduplicated by player
// (only the best score per player counts). Rank is 1-based.
export function getAllTimeLeaderboard(limit = 10): LeaderboardRow[] {
	const stmt = db.prepare(`
		SELECT player_name, MAX(max_speed) AS best
		FROM scores
		GROUP BY player_name
		ORDER BY best DESC
		LIMIT ?1
	`);
	const rows = stmt.all(limit) as Array<{ player_name: string; best: number }>;
	return rows.map((r, i) => ({
		rank: i + 1,
		name: r.player_name,
		maxSpeed: Math.round(r.best),
	}));
}

// Get a single player's all-time best speed (used to craft the score_ack).
export function allTimeBestForPlayer(playerName: string): number {
	const stmt = db.prepare(`
		SELECT MAX(max_speed) AS best
		FROM scores
		WHERE player_name = ?1
	`);
	const row = stmt.get(playerName) as { best: number | null } | undefined;
	return row?.best ?? 0;
}

// Find the all-time rank of a given player (1-based, 0 if not ranked).
export function allTimeRankForPlayer(playerName: string): number {
	const stmt = db.prepare(`
		SELECT COUNT(*) + 1 AS rank
		FROM (
			SELECT player_name, MAX(max_speed) AS best
			FROM scores
			GROUP BY player_name
		)
		WHERE best > (
			SELECT COALESCE(MAX(max_speed), 0)
			FROM scores
			WHERE player_name = ?1
		)
	`);
	const row = stmt.get(playerName) as { rank: number } | undefined;
	return row?.rank ?? 1;
}

// -- RG Challenge leaderboard (separate table, same pattern) --

// Ensure the rg_scores table exists (runs on first access).
function ensureRgTable(): void {
	db.run(`
		CREATE TABLE IF NOT EXISTS rg_scores (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			date TEXT NOT NULL,
			player_name TEXT NOT NULL,
			max_streak INTEGER NOT NULL,
			timestamp INTEGER NOT NULL
		)
	`);
	db.run(`CREATE INDEX IF NOT EXISTS idx_rg_scores_date_streak ON rg_scores(date, max_streak DESC)`);
	db.run(`CREATE INDEX IF NOT EXISTS idx_rg_scores_date_name ON rg_scores(date, player_name)`);
}

export function submitRgScore(
	date: string,
	playerName: string,
	maxStreak: number,
): void {
	ensureRgTable();
	const stmt = db.prepare(`
		INSERT INTO rg_scores (date, player_name, max_streak, timestamp)
		VALUES (?1, ?2, ?3, ?4)
	`);
	stmt.run(date, playerName, maxStreak, Date.now());
}

export function getRgAllTimeLeaderboard(
	limit = 10,
): Array<{ rank: number; name: string; maxStreak: number }> {
	ensureRgTable();
	const stmt = db.prepare(`
		SELECT player_name, MAX(max_streak) AS best
		FROM rg_scores
		GROUP BY player_name
		ORDER BY best DESC
		LIMIT ?1
	`);
	const rows = stmt.all(limit) as Array<{
		player_name: string;
		best: number;
	}>;
	return rows.map((r, i) => ({
		rank: i + 1,
		name: r.player_name,
		maxStreak: Math.round(r.best),
	}));
}

export function rgAllTimeBestForPlayer(
	playerName: string,
): number {
	ensureRgTable();
	const stmt = db.prepare(`
		SELECT MAX(max_streak) AS best
		FROM rg_scores
		WHERE player_name = ?1
	`);
	const row = stmt.get(playerName) as { best: number | null } | undefined;
	return row?.best ?? 0;
}

export function rgAllTimeRankForPlayer(
	playerName: string,
): number {
	ensureRgTable();
	const stmt = db.prepare(`
		SELECT COUNT(*) + 1 AS rank
		FROM (
			SELECT player_name, MAX(max_streak) AS best
			FROM rg_scores
			GROUP BY player_name
		)
		WHERE best > (
			SELECT COALESCE(MAX(max_streak), 0)
			FROM rg_scores
			WHERE player_name = ?1
		)
	`);
	const row = stmt.get(playerName) as { rank: number } | undefined;
	return row?.rank ?? 1;
}
