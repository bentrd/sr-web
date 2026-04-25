// Map list — hardcoded for v1. The 4 maps the project ships with.
// Source of truth for the picker; the actual .sr files live under
// `game/assets/maps/` (gitignored, populated by `bun run collect-maps`).

export type MapEntry = { id: string; displayName: string };

export const MAPS: readonly MapEntry[] = [
	{ id: "pitfall", displayName: "Pitfall" },
	{ id: "genetics", displayName: "Genetics" },
	{ id: "grapple_circuit", displayName: "Grapple Circuit" },
	{ id: "oasis_abyss", displayName: "Oasis Abyss" },
];
