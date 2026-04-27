// Injects the Emscripten-emitted sr.js as a regular script tag so the
// global `createSrModule` factory becomes available, then awaits the
// module instance with the canvas wired up. We deliberately don't ship
// `-sEXPORT_ES6=1` because that requires Vite-side glue we don't need.
//
// The script is loaded once per page; subsequent calls reuse the same
// promise. createSrModule itself can be invoked multiple times, but we
// never need more than one game instance at a time.

export interface SrModule {
	cwrap: (
		name: string,
		ret: string | null,
		args: readonly string[],
	) => (...a: unknown[]) => unknown;
	HEAPU8: Uint8Array;
	HEAPF32: Float32Array;
	stringToUTF8: (s: string, ptr: number, len: number) => void;
	UTF8ToString: (ptr: number, len?: number) => string;
	_malloc: (n: number) => number;
	_free: (ptr: number) => void;
	canvas?: HTMLCanvasElement;
}

interface ModuleOpts {
	canvas: HTMLCanvasElement;
	// `sr.js` requests its companion files (sr.wasm, sr.data) by relative
	// name. The lobby lives at /r/CODE so that resolves to
	// /r/CODE/sr.data → Vite's SPA fallback returns index.html and the
	// file-packager silently fails. Anchor everything at /.
	locateFile: (path: string) => string;
}

type CreateSrModule = (opts: ModuleOpts) => Promise<SrModule>;

declare global {
	interface Window {
		createSrModule?: CreateSrModule;
	}
	// Injected by Vite at build time — see vite.config.ts.
	const __SR_BUILD_ID__: string;
}

// Cache-buster appended to sr.js / sr.wasm / sr.data so each deploy is
// fetched fresh. Without this, GH Pages' max-age=600 + the fixed
// filenames mean clients can ride a stale wasm for up to 10 minutes
// after a deploy.
const BUILD_ID =
	typeof __SR_BUILD_ID__ !== "undefined" ? __SR_BUILD_ID__ : "dev";

// `BASE_URL` is the public path Vite was built with — `/` in dev, `/sr-web/`
// for GH Pages. Always trailing-slashed. We need to anchor sr.js + its
// companion files (sr.wasm, sr.data) here so they resolve regardless of the
// current SPA route.
const BASE = import.meta.env.BASE_URL;

let scriptPromise: Promise<CreateSrModule> | null = null;

function loadScript(): Promise<CreateSrModule> {
	if (scriptPromise) return scriptPromise;
	scriptPromise = new Promise<CreateSrModule>((resolve, reject) => {
		if (window.createSrModule) {
			resolve(window.createSrModule);
			return;
		}
		const tag = document.createElement("script");
		tag.src = `${BASE}sr.js?v=${BUILD_ID}`;
		tag.async = true;
		tag.onload = (): void => {
			if (!window.createSrModule) {
				reject(new Error("sr.js loaded but createSrModule not on window"));
				return;
			}
			resolve(window.createSrModule);
		};
		tag.onerror = (): void => {
			reject(new Error(`Failed to load ${BASE}sr.js — is build:wasm green?`));
			scriptPromise = null;
		};
		document.head.appendChild(tag);
	});
	return scriptPromise;
}

// One Emscripten instance per canvas, ever. createSrModule starts a _main
// loop on construction — calling it twice (e.g. React StrictMode's
// mount→unmount→mount in dev) leaves two parallel sims running on the same
// window keydown listeners with two independent input_map globals. The
// orphaned instance keeps using defaults; the live instance gets the
// rebound input_map. Symptom: visible canvas drives off the orphan,
// abiRef.current / getLocalSnapshot read the live one.
const moduleByCanvas = new WeakMap<HTMLCanvasElement, Promise<SrModule>>();

export async function loadSrModule(canvas: HTMLCanvasElement): Promise<SrModule> {
	const cached = moduleByCanvas.get(canvas);
	if (cached) return cached;
	const promise = loadScript().then((factory) =>
		factory({
			canvas,
			locateFile: (path) => `${BASE}${path}?v=${BUILD_ID}`,
		}),
	);
	moduleByCanvas.set(canvas, promise);
	return promise;
}
