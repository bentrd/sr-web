// Crockford base32 alphabet excluding I, L, O, U (visually ambiguous /
// vulgar). 5-char codes give 32^5 = ~33M combinations — plenty.

const ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
const CODE_LEN = 5;

export function generateCode(): string {
	let s = "";
	for (let i = 0; i < CODE_LEN; i++) {
		s += ALPHABET[Math.floor(Math.random() * ALPHABET.length)];
	}
	return s;
}

export function generateUniqueCode(taken: ReadonlySet<string>): string {
	for (let attempt = 0; attempt < 50; attempt++) {
		const c = generateCode();
		if (!taken.has(c)) return c;
	}
	throw new Error("could not generate unique room code after 50 attempts");
}

// Crockford normalisation: uppercase, strip ambiguous characters by
// substitution (1↔I/L, 0↔O). Lets us be lenient with user input.
export function normaliseCode(input: string): string {
	return input
		.toUpperCase()
		.replace(/[IL]/g, "1")
		.replace(/O/g, "0")
		.replace(/U/g, "V")
		.replace(/[^0-9A-Z]/g, "");
}
