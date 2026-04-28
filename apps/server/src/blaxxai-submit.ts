// One-off helper: convert a CSV input log (one row per sim tick at 300 Hz)
// into the run-recorder's wire format, deterministically replay it through
// the server's replay binary to obtain max_speed, and emit a base64-encoded
// blob suitable for direct DB insertion.
//
// As of sim_version 2 the validator requires a starting savestate. Pass
// the path via `--savestate=<file>`. The savestate file must be the raw
// bytes produced by playground::capture_savestate (magic 'PSAV').
//
// Usage:
//   bun run src/blaxxai-submit.ts <csv-path> --savestate=<path> [--ticks=3300]
//
// CSV columns expected (case-insensitive, in this order after the row meta):
//   left, right, jump, grapple, slide, boost, item, taunt
// where `taunt` is the swap-item bit (bit 7 in the wire format).

import { replayRun } from "./replay";

const inputBitOrder = ["left", "right", "jump", "grapple", "slide", "boost", "item", "taunt"] as const;

interface Args {
	csvPath: string;
	tickLimit: number;
	savestatePath: string;
}

function parseArgs(): Args {
	const args = process.argv.slice(2);
	const csvPath = args.find((a) => !a.startsWith("--"));
	if (!csvPath) {
		console.error("usage: bun src/blaxxai-submit.ts <csv-path> --savestate=<path> [--ticks=N]");
		process.exit(1);
	}
	const ticksArg = args.find((a) => a.startsWith("--ticks="));
	const tickLimit = ticksArg ? Number(ticksArg.slice("--ticks=".length)) : 3300;
	if (!Number.isInteger(tickLimit) || tickLimit <= 0) {
		console.error("invalid --ticks");
		process.exit(1);
	}
	const ssArg = args.find((a) => a.startsWith("--savestate="));
	const savestatePath = ssArg ? ssArg.slice("--savestate=".length) : "";
	if (!savestatePath) {
		console.error("--savestate=<path> is required (sim_version 2)");
		process.exit(1);
	}
	return { csvPath, tickLimit, savestatePath };
}

async function main(): Promise<void> {
	const { csvPath, tickLimit, savestatePath } = parseArgs();
	const text = await Bun.file(csvPath).text();
	const lines = text.split(/\r?\n/).filter((l) => l.trim().length > 0);
	const headerLine = lines[0];
	if (headerLine === undefined) throw new Error("CSV is empty");
	const header = headerLine.split(",").map((h) => h.trim().toLowerCase());
	const colIdx = inputBitOrder.map((name) => {
		const i = header.indexOf(name);
		if (i < 0) throw new Error(`CSV missing column: ${name}`);
		return i;
	});

	const rows = lines.slice(1, 1 + tickLimit);
	if (rows.length < tickLimit) {
		console.warn(`CSV only has ${rows.length} rows (< requested ${tickLimit})`);
	}

	const log: number[] = [];
	const writeVarint = (n: number): void => {
		while (n >= 0x80) {
			log.push((n & 0x7f) | 0x80);
			n >>>= 7;
		}
		log.push(n & 0x7f);
	};
	const bitmaskFor = (row: string[]): number => {
		let bm = 0;
		for (let bit = 0; bit < inputBitOrder.length; bit++) {
			const idx = colIdx[bit];
			if (idx === undefined) continue;
			const cell = row[idx];
			if (cell !== undefined && cell.trim().toLowerCase() === "true") {
				bm |= 1 << bit;
			}
		}
		return bm;
	};

	const firstRow = rows[0];
	if (firstRow === undefined) throw new Error("CSV has no data rows");
	let lastEventRow = 1; // 1-indexed: seed corresponds to row 1
	let lastBitmask = bitmaskFor(firstRow.split(","));
	writeVarint(0);
	log.push(lastBitmask);

	for (let i = 1; i < rows.length; i++) {
		const row = rows[i];
		if (row === undefined) continue;
		const bm = bitmaskFor(row.split(","));
		if (bm !== lastBitmask) {
			const rowNum = i + 1; // 1-indexed
			const delta = rowNum - lastEventRow;
			writeVarint(delta);
			log.push(bm);
			lastEventRow = rowNum;
			lastBitmask = bm;
		}
	}

	const inputs = new Uint8Array(log);
	const durationTicks = rows.length;
	const savestate = new Uint8Array(await Bun.file(savestatePath).arrayBuffer());

	console.log(`encoded log: ${inputs.length} bytes, ${durationTicks} ticks (~${(durationTicks/300).toFixed(2)}s)`);
	console.log(`savestate: ${savestate.length} bytes`);

	const r = await replayRun(inputs, durationTicks, savestate);
	if (!r.ok) {
		console.error("replay failed:", r.error);
		process.exit(1);
	}
	console.log(`replayed max_speed: ${r.maxSpeed.toFixed(2)} wu/s (rounded: ${Math.round(r.maxSpeed)})`);

	const inputsB64 = Buffer.from(inputs).toString("base64");
	const savestateB64 = Buffer.from(savestate).toString("base64");
	console.log("--- output ---");
	console.log(JSON.stringify({
		durationTicks,
		claimedMaxSpeed: Math.round(r.maxSpeed),
		simVersion: 2,
		inputsB64,
		savestateB64,
	}, null, 2));
}

void main();
