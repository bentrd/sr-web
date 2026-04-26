// Parser for SpeedRunners' .trail binary format. The file is a flat
// header (name, author, an 8-byte mystery blob, and the trail's icon
// keyframe table) followed by one block per layer. Each layer is a
// uint32-prefixed array of (key, value) string pairs.
//
// Layout (little-endian throughout, all strings are uint8 length + UTF-8):
//
//   uint32 version           // observed: 5
//   string name              // e.g. "ST Goldilocks"
//   string author            // e.g. "Olsu"
//   string description       // empty in every workshop trail we've seen
//   uint8[8] mystery         // skipped — looks like a timestamp/GUID tail
//   string "icon"            // literal field key
//   uint32 iconCount         // N
//   string[N*2] iconEntries  // (key, value) pairs, both filenames
//   uint32 layerCount        // N
//   for each layer:
//     uint8 layerSep         // always 0x00
//     uint32 propCount       // N
//     string[N*2] propPairs  // (key, value) — values are stringly-typed
//
// Property values come out of the file as strings ("TRUE", "1", "1,1,1");
// callers get a typed TrailLayer with all the parsing baked in. Unknown
// keys are tolerated — the format is open-ended on the SpeedRunners side
// and the editor adds new properties over time.

export type EnabledMode = "ALWAYS" | "ONLY AT SUPERSPEED";

export interface TrailLayer {
	imageName: string;
	visible: boolean;
	enabledMode: EnabledMode;
	lifetime: number; // seconds
	color: [number, number, number]; // 0..1
	opacity: number; // 0..1
	size: number; // pixels
	taper: boolean;
	fadeOut: boolean;
	fadeOutSpeed: number;
	flipH: boolean;
	flipV: boolean;
	forceRightSideUp: boolean;
	offsetVector: [number, number];
	invertOffset: boolean;
}

export interface TrailDefinition {
	name: string;
	author: string;
	layers: TrailLayer[];
}

class Reader {
	private readonly view: DataView;
	private readonly bytes: Uint8Array;
	private offset = 0;

	constructor(buffer: ArrayBuffer) {
		this.view = new DataView(buffer);
		this.bytes = new Uint8Array(buffer);
	}

	get pos(): number {
		return this.offset;
	}

	get length(): number {
		return this.bytes.byteLength;
	}

	readU8(): number {
		const v = this.view.getUint8(this.offset);
		this.offset += 1;
		return v;
	}

	readU32LE(): number {
		const v = this.view.getUint32(this.offset, true);
		this.offset += 4;
		return v;
	}

	skip(n: number): void {
		this.offset += n;
	}

	// uint8 length prefix + UTF-8 bytes. Trail strings observed in the
	// wild stay well under 256 bytes, so we don't worry about the wider
	// LEB128-style encoding .NET uses for longer strings.
	readString(): string {
		const len = this.readU8();
		const slice = this.bytes.subarray(this.offset, this.offset + len);
		this.offset += len;
		return new TextDecoder("utf-8").decode(slice);
	}
}

function parseFloat01(s: string): number {
	const n = Number.parseFloat(s);
	if (!Number.isFinite(n)) return 0;
	return Math.max(0, Math.min(1, n));
}

function parseFloatNonNeg(s: string): number {
	const n = Number.parseFloat(s);
	if (!Number.isFinite(n) || n < 0) return 0;
	return n;
}

function parseBool(s: string): boolean {
	return s.trim().toUpperCase() === "TRUE";
}

function parseColor(s: string): [number, number, number] {
	// "1,1,1" — three comma-separated 0..1 floats.
	const parts = s.split(",").map((p) => parseFloat01(p.trim()));
	return [parts[0] ?? 1, parts[1] ?? 1, parts[2] ?? 1];
}

function parseVec2(s: string): [number, number] {
	const parts = s.split(",").map((p) => Number.parseFloat(p.trim()));
	const x = Number.isFinite(parts[0]) ? (parts[0] as number) : 0;
	const y = Number.isFinite(parts[1]) ? (parts[1] as number) : 0;
	return [x, y];
}

function parseEnabledMode(s: string): EnabledMode {
	return s.trim().toUpperCase() === "ONLY AT SUPERSPEED" ? "ONLY AT SUPERSPEED" : "ALWAYS";
}

function layerFromProps(props: Map<string, string>): TrailLayer {
	// Defaults match what an "untouched" layer looks like in the SR trail
	// editor: opaque white, 1s lifetime, 32px wide, no flips, no offset.
	return {
		imageName: props.get("Image") ?? "",
		visible: parseBool(props.get("Visible") ?? "TRUE"),
		enabledMode: parseEnabledMode(props.get("Enabled") ?? "ALWAYS"),
		lifetime: parseFloatNonNeg(props.get("LifeTime") ?? "1"),
		color: parseColor(props.get("Color") ?? "1,1,1"),
		opacity: parseFloat01(props.get("Opacity") ?? "1"),
		size: parseFloatNonNeg(props.get("Size") ?? "32"),
		taper: parseBool(props.get("Taper") ?? "FALSE"),
		fadeOut: parseBool(props.get("FadeOut") ?? "FALSE"),
		fadeOutSpeed: parseFloatNonNeg(props.get("FadeOut Speed") ?? "1"),
		flipH: parseBool(props.get("Flip Horizontally") ?? "FALSE"),
		flipV: parseBool(props.get("Flip Vertically") ?? "FALSE"),
		forceRightSideUp: parseBool(props.get("Force right side Up") ?? "FALSE"),
		offsetVector: parseVec2(props.get("OffsetVector") ?? "0,0"),
		invertOffset: parseBool(props.get("Invert Offset") ?? "FALSE"),
	};
}

export function parseTrail(buffer: ArrayBuffer): TrailDefinition {
	const r = new Reader(buffer);

	r.readU32LE(); // version (5 in every file we've seen — not validated; open-ended on the SR side)
	const name = r.readString();
	const author = r.readString();
	r.readString(); // description (always empty in workshop trails)
	r.skip(8); // mystery blob — likely a timestamp tail; not needed for rendering

	// "icon" key + iconCount × 2 strings. We don't use the icon table for
	// in-game rendering, but we have to walk it to reach the layers.
	r.readString(); // literal "icon"
	const iconCount = r.readU32LE();
	for (let i = 0; i < iconCount * 2; i++) r.readString();

	const layerCount = r.readU32LE();
	const layers: TrailLayer[] = [];
	for (let l = 0; l < layerCount; l++) {
		r.readU8(); // layer separator (always 0x00)
		const propCount = r.readU32LE();
		const props = new Map<string, string>();
		for (let p = 0; p < propCount; p++) {
			const key = r.readString();
			const value = r.readString();
			props.set(key, value);
		}
		layers.push(layerFromProps(props));
	}

	return { name, author, layers };
}
