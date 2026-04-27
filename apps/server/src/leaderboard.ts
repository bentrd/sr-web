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
	// Best matching run id for this player (the run with claimed_max_speed
	// equal to the leaderboard's best). null when no recorded run exists
	// (e.g. legacy scores submitted before the runs table was added).
	runId: number | null;
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
	ensureRunsTable();
	// First aggregate the per-player best in a CTE, then join to runs.
	// SQLite can't reference an aggregate inside a correlated subquery, so
	// we materialize `best` first and use it as a regular column for the
	// run lookup. Latest matching run wins on ties (re-runs at the same
	// rounded speed return the freshest replay).
	const stmt = db.prepare(`
		WITH best_per_player AS (
			SELECT player_name, MAX(max_speed) AS best
			FROM scores
			GROUP BY player_name
		)
		SELECT b.player_name AS player_name,
		       b.best AS best,
		       (SELECT r.id FROM runs r
		        WHERE r.player_name = b.player_name
		          AND ROUND(r.claimed_max_speed) = ROUND(b.best)
		        ORDER BY r.id DESC LIMIT 1) AS run_id
		FROM best_per_player b
		ORDER BY b.best DESC
		LIMIT ?1
	`);
	const rows = stmt.all(limit) as Array<{
		player_name: string;
		best: number;
		run_id: number | null;
	}>;
	return rows.map((r, i) => ({
		rank: i + 1,
		name: r.player_name,
		maxSpeed: Math.round(r.best),
		runId: r.run_id ?? null,
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
): Array<{ rank: number; name: string; maxStreak: number; runId: number | null }> {
	ensureRgTable();
	ensureRgRunsTable();
	// Same shape as the speed leaderboard: aggregate per-player best in a
	// CTE first, then join to rg_runs. SQLite rejects aggregate refs inside
	// correlated subqueries, so we materialize `best` first.
	const stmt = db.prepare(`
		WITH best_per_player AS (
			SELECT player_name, MAX(max_streak) AS best
			FROM rg_scores
			GROUP BY player_name
		)
		SELECT b.player_name AS player_name,
		       b.best AS best,
		       (SELECT r.id FROM rg_runs r
		        WHERE r.player_name = b.player_name
		          AND r.claimed_max_streak = b.best
		        ORDER BY r.id DESC LIMIT 1) AS run_id
		FROM best_per_player b
		ORDER BY b.best DESC
		LIMIT ?1
	`);
	const rows = stmt.all(limit) as Array<{
		player_name: string;
		best: number;
		run_id: number | null;
	}>;
	return rows.map((r, i) => ({
		rank: i + 1,
		name: r.player_name,
		maxStreak: Math.round(r.best),
		runId: r.run_id ?? null,
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

// -- Anti-cheat run storage ----------------------------------------------
//
// Holds the raw input stream submitted at the end of each PR-beating
// grapple-challenge run. Replay validation is done out-of-band (Phase 2);
// for now the table just collects evidence so we can analyse a backlog
// of real input streams before turning replay on.

function ensureRunsTable(): void {
	db.run(`
		CREATE TABLE IF NOT EXISTS runs (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			date TEXT NOT NULL,
			player_name TEXT NOT NULL,
			claimed_max_speed REAL NOT NULL,
			duration_ticks INTEGER NOT NULL,
			sim_version INTEGER NOT NULL,
			inputs BLOB NOT NULL,
			verified INTEGER NOT NULL DEFAULT 0,
			timestamp INTEGER NOT NULL
		)
	`);
	db.run(`CREATE INDEX IF NOT EXISTS idx_runs_date_speed ON runs(date, claimed_max_speed DESC)`);
	db.run(`CREATE INDEX IF NOT EXISTS idx_runs_player ON runs(player_name)`);
}

// RG-mode counterpart to runs. Stores the input log + claimed max_streak
// so the server can replay-validate the run later. `verified` semantics
// match the speed-run table (1=match, -1=diverge, 0=unknown/error).
function ensureRgRunsTable(): void {
	db.run(`
		CREATE TABLE IF NOT EXISTS rg_runs (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			date TEXT NOT NULL,
			player_name TEXT NOT NULL,
			claimed_max_streak INTEGER NOT NULL,
			duration_ticks INTEGER NOT NULL,
			sim_version INTEGER NOT NULL,
			inputs BLOB NOT NULL,
			verified INTEGER NOT NULL DEFAULT 0,
			timestamp INTEGER NOT NULL
		)
	`);
	db.run(`CREATE INDEX IF NOT EXISTS idx_rg_runs_date_streak ON rg_runs(date, claimed_max_streak DESC)`);
	db.run(`CREATE INDEX IF NOT EXISTS idx_rg_runs_player ON rg_runs(player_name)`);
}

export function submitRgRun(
	date: string,
	playerName: string,
	claimedMaxStreak: number,
	durationTicks: number,
	simVersion: number,
	inputs: Uint8Array,
): number {
	ensureRgRunsTable();
	const r = db
		.prepare(
			`INSERT INTO rg_runs (date, player_name, claimed_max_streak, duration_ticks, sim_version, inputs, timestamp)
			 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)`,
		)
		.run(date, playerName, claimedMaxStreak, durationTicks, simVersion, inputs, Date.now());
	return Number(r.lastInsertRowid);
}

export function markRgRunVerified(id: number, verified: 1 | -1): boolean {
	ensureRgRunsTable();
	return db
		.prepare("UPDATE rg_runs SET verified = ? WHERE id = ?")
		.run(verified, id).changes > 0;
}

// Public-readable shape for the GET /run/:id endpoint. Inputs are the
// raw blob bytes; the HTTP layer base64-encodes them for transport.
export interface RunRecord {
	id: number;
	playerName: string;
	claimedMaxSpeed: number;
	durationTicks: number;
	simVersion: number;
	verified: number;
	timestamp: number;
	inputs: Uint8Array;
}

export interface RgRunRecord {
	id: number;
	playerName: string;
	claimedMaxStreak: number;
	durationTicks: number;
	simVersion: number;
	verified: number;
	timestamp: number;
	inputs: Uint8Array;
}

export function getRunById(id: number): RunRecord | null {
	ensureRunsTable();
	const row = db
		.prepare(
			`SELECT id, player_name, claimed_max_speed, duration_ticks, sim_version,
			        verified, timestamp, inputs
			 FROM runs WHERE id = ?`,
		)
		.get(id) as
		| {
			id: number;
			player_name: string;
			claimed_max_speed: number;
			duration_ticks: number;
			sim_version: number;
			verified: number;
			timestamp: number;
			inputs: Uint8Array;
		}
		| undefined;
	if (!row) return null;
	return {
		id: row.id,
		playerName: row.player_name,
		claimedMaxSpeed: row.claimed_max_speed,
		durationTicks: row.duration_ticks,
		simVersion: row.sim_version,
		verified: row.verified,
		timestamp: row.timestamp,
		inputs: row.inputs,
	};
}

export function getRgRunById(id: number): RgRunRecord | null {
	ensureRgRunsTable();
	const row = db
		.prepare(
			`SELECT id, player_name, claimed_max_streak, duration_ticks, sim_version,
			        verified, timestamp, inputs
			 FROM rg_runs WHERE id = ?`,
		)
		.get(id) as
		| {
			id: number;
			player_name: string;
			claimed_max_streak: number;
			duration_ticks: number;
			sim_version: number;
			verified: number;
			timestamp: number;
			inputs: Uint8Array;
		}
		| undefined;
	if (!row) return null;
	return {
		id: row.id,
		playerName: row.player_name,
		claimedMaxStreak: row.claimed_max_streak,
		durationTicks: row.duration_ticks,
		simVersion: row.sim_version,
		verified: row.verified,
		timestamp: row.timestamp,
		inputs: row.inputs,
	};
}

// Mark an existing run as verified or rejected.
//   verified =  1 → replay matched claimed speed (within tolerance)
//   verified = -1 → replay diverged → suspected cheat
//   verified =  0 → unknown / error during replay (default)
export function markRunVerified(id: number, verified: 1 | -1): boolean {
	ensureRunsTable();
	return db
		.prepare("UPDATE runs SET verified = ? WHERE id = ?")
		.run(verified, id).changes > 0;
}

// Insert a recorded run. Returns the new row id.
export function submitRun(
	date: string,
	playerName: string,
	claimedMaxSpeed: number,
	durationTicks: number,
	simVersion: number,
	inputs: Uint8Array,
): number {
	ensureRunsTable();
	const r = db
		.prepare(
			`INSERT INTO runs (date, player_name, claimed_max_speed, duration_ticks, sim_version, inputs, timestamp)
			 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)`,
		)
		.run(
			date,
			playerName,
			claimedMaxSpeed,
			durationTicks,
			simVersion,
			inputs,
			Date.now(),
		);
	return Number(r.lastInsertRowid);
}

// -- Admin CRUD (raw, unvalidated — for /admin only) -------------------

export interface RawScore {
	id: number;
	date: string;
	player_name: string;
	max_speed: number;
	timestamp: number;
}

export interface RawRgScore {
	id: number;
	date: string;
	player_name: string;
	max_streak: number;
	timestamp: number;
}

export function adminListScores(): RawScore[] {
	return db
		.prepare(
			"SELECT id, date, player_name, max_speed, timestamp FROM scores ORDER BY id DESC",
		)
		.all() as RawScore[];
}

export function adminListRgScores(): RawRgScore[] {
	ensureRgTable();
	return db
		.prepare(
			"SELECT id, date, player_name, max_streak, timestamp FROM rg_scores ORDER BY id DESC",
		)
		.all() as RawRgScore[];
}

const SCORE_FIELDS = ["date", "player_name", "max_speed", "timestamp"] as const;
const RG_SCORE_FIELDS = ["date", "player_name", "max_streak", "timestamp"] as const;

function buildUpdate(
	table: "scores" | "rg_scores",
	allowed: readonly string[],
	id: number,
	fields: Record<string, unknown>,
): boolean {
	const sets: string[] = [];
	const vals: unknown[] = [];
	for (const k of allowed) {
		if (Object.prototype.hasOwnProperty.call(fields, k)) {
			sets.push(`${k} = ?`);
			vals.push(fields[k]);
		}
	}
	if (sets.length === 0) return false;
	vals.push(id);
	const stmt = db.prepare(`UPDATE ${table} SET ${sets.join(", ")} WHERE id = ?`);
	return stmt.run(...(vals as never[])).changes > 0;
}

export function adminUpdateScore(id: number, fields: Record<string, unknown>): boolean {
	return buildUpdate("scores", SCORE_FIELDS, id, fields);
}

export function adminUpdateRgScore(id: number, fields: Record<string, unknown>): boolean {
	ensureRgTable();
	return buildUpdate("rg_scores", RG_SCORE_FIELDS, id, fields);
}

export function adminDeleteScore(id: number): boolean {
	return db.prepare("DELETE FROM scores WHERE id = ?").run(id).changes > 0;
}

export function adminDeleteRgScore(id: number): boolean {
	ensureRgTable();
	return db.prepare("DELETE FROM rg_scores WHERE id = ?").run(id).changes > 0;
}

export function adminInsertScore(
	date: string,
	playerName: string,
	maxSpeed: number,
	timestamp: number,
): number {
	const r = db
		.prepare(
			"INSERT INTO scores (date, player_name, max_speed, timestamp) VALUES (?, ?, ?, ?)",
		)
		.run(date, playerName, maxSpeed, timestamp);
	return Number(r.lastInsertRowid);
}

export function adminInsertRgScore(
	date: string,
	playerName: string,
	maxStreak: number,
	timestamp: number,
): number {
	ensureRgTable();
	const r = db
		.prepare(
			"INSERT INTO rg_scores (date, player_name, max_streak, timestamp) VALUES (?, ?, ?, ?)",
		)
		.run(date, playerName, maxStreak, timestamp);
	return Number(r.lastInsertRowid);
}
