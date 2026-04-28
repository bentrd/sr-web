// One-off helper: convert a CSV input log (one row per sim tick at 300 Hz)
// into the run-recorder's wire format, deterministically replay it through
// the server's replay binary to obtain max_speed, and emit a base64-encoded
// blob suitable for direct DB insertion.
//
// Usage:
//   bun run src/blaxxai-submit.ts <csv-path> [--ticks=3300]
//
// CSV columns expected (case-insensitive, in this order after the row meta):
//   left, right, jump, grapple, slide, boost, item, taunt
// where `taunt` is the swap-item bit (bit 7 in the wire format).

import { replayRun } from "./replay";

const inputBitOrder = ["left", "right", "jump", "grapple", "slide", "boost", "item", "taunt"] as const;

function parseArgs(): { csvPath: string; tickLimit: number } {
	const args = process.argv.slice(2);
	const csvPath = args.find((a) => !a.startsWith("--"));
	if (!csvPath) {
		console.error("usage: bun src/blaxxai-submit.ts <csv-path> [--ticks=N]");
		process.exit(1);
	}
	const ticksArg = args.find((a) => a.startsWith("--ticks="));
	const tickLimit = ticksArg ? Number(ticksArg.slice("--ticks=".length)) : 3300;
	if (!Number.isInteger(tickLimit) || tickLimit <= 0) {
		console.error("invalid --ticks");
		process.exit(1);
	}
	return { csvPath, tickLimit };
}

async function main(): Promise<void> {
	const { csvPath, tickLimit } = parseArgs();
	const text = await Bun.file(csvPath).text();
	const lines = text.split(/\r?\n/).filter((l) => l.trim().length > 0);
	const header = lines[0].split(",").map((h) => h.trim().toLowerCase());
	const colIdx = inputBitOrder.map((name) => {
		const i = header.indexOf(name);
		if (i < 0) throw new Error(`CSV missing column: ${name}`);
		return i;
	});

	const rows = lines.slice(1, 1 + tickLimit);
	if (rows.length < tickLimit) {
		console.warn(`CSV only has ${rows.length} rows (< requested ${tickLimit})`);
	}

	// Build the log: seed event (delta=0, bitmask) + delta+bitmask on each
	// change. Mirrors run_recorder::append_event so the replay binary can
	// decode it bit-exact.
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
			const cell = row[colIdx[bit]];
			if (cell !== undefined && cell.trim().toLowerCase() === "true") {
				bm |= 1 << bit;
			}
		}
		return bm;
	};

	let lastEventRow = 1; // 1-indexed: seed corresponds to row 1
	let lastBitmask = bitmaskFor(rows[0].split(","));
	writeVarint(0);
	log.push(lastBitmask);

	for (let i = 1; i < rows.length; i++) {
		const bm = bitmaskFor(rows[i].split(","));
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

	console.log(`encoded log: ${inputs.length} bytes, ${durationTicks} ticks (~${(durationTicks/300).toFixed(2)}s)`);

	const r = await replayRun(inputs, durationTicks);
	if (!r.ok) {
		console.error("replay failed:", r.error);
		process.exit(1);
	}
	console.log(`replayed max_speed: ${r.maxSpeed.toFixed(2)} wu/s (rounded: ${Math.round(r.maxSpeed)})`);

	const b64 = Buffer.from(inputs).toString("base64");
	console.log("--- output ---");
	console.log(JSON.stringify({
		durationTicks,
		claimedMaxSpeed: Math.round(r.maxSpeed),
		simVersion: 1,
		lastRunStartTick: 0,
		inputsB64: b64,
	}, null, 2));
}

void main();
