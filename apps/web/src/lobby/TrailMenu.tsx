// The lobby's trail picker — replaces the previous one-shot file-picker
// pill with a dropdown menu. Behaviour summary:
//   - Pill default: icon + name of the current trail (or "✦ Trail"
//     placeholder when nothing is picked).
//   - Pill hover: morphs to an upload affordance ("↑ Upload") so it's
//     obvious uploads still work — uploading is one of the dropdown
//     items, but the hover hint avoids hiding it behind a click.
//   - Click: opens a dropdown with Presets, the user's recently
//     uploaded trails, an "Upload from folder…" item, and a Clear item.
//   - Right-click: instant clear (preserves the old behaviour).
//
// Presets and uploads both end up in `identity.trail` as a base64 .srt
// blob, so Game.tsx's existing trail-load + WS share path doesn't care
// where the blob came from.

import { useEffect, useMemo, useRef, useState } from "react";
import { useApp } from "../state/AppState";
import { parseSrt, buildSrt, bytesToBase64 } from "../game/trail/parseSrt";
import type { TrailManifestEntry } from "../game/trail/loadTrail";
import { fetchPresetAsSrt, bytesToPngDataUrl } from "../game/trail/preset";
import {
	addSavedTrail,
	removeSavedTrail,
	type SavedTrail,
} from "../state/savedTrails";

// Server-side hard cap (~285 KB raw → 384 KB base64). Keep in sync with
// the validator in apps/server/src/messages.ts.
const TRAIL_MAX_BYTES = 285 * 1024;

const BASE = import.meta.env.BASE_URL;

async function fetchManifest(): Promise<TrailManifestEntry[]> {
	const res = await fetch(`${BASE}trails/manifest.json`);
	if (!res.ok) throw new Error(`manifest fetch failed: ${res.status}`);
	return (await res.json()) as TrailManifestEntry[];
}

interface TrailBlob {
	bytes: Uint8Array;
	displayName: string;
}

// Reads a folder pick (FileList) into a single .srt blob. Handles both
// shapes the user might give us: a real .srt zip (e.g. one shared) or
// the unzipped Steam Cloud layout (settings.trail + PNGs + icon).
async function readTrailFromFolder(fileList: FileList): Promise<TrailBlob> {
	const files = Array.from(fileList);
	const realSrt = files.find((f) => {
		const n = f.name.toLowerCase();
		// .srt files in CEngineStorage/.../Local/ are 22-byte stub zips;
		// the size guard skips those without trying to unzip them.
		return n.endsWith(".srt") && f.size > 22;
	});
	const settings = files.find((f) => f.name.toLowerCase() === "settings.trail");

	if (realSrt) {
		if (realSrt.size > TRAIL_MAX_BYTES) throw new Error("file is over 285 KB");
		return {
			bytes: new Uint8Array(await realSrt.arrayBuffer()),
			displayName: realSrt.name.replace(/\.srt$/i, ""),
		};
	}
	if (settings) {
		const wanted = files.filter((f) => {
			const n = f.name.toLowerCase();
			return n === "settings.trail" || n === "icon" || n.endsWith(".png");
		});
		const built = await Promise.all(
			wanted.map(async (f) => ({
				name: f.name,
				bytes: new Uint8Array(await f.arrayBuffer()),
			})),
		);
		const bytes = buildSrt(built);
		if (bytes.byteLength > TRAIL_MAX_BYTES) throw new Error("zipped folder is over 285 KB");
		const rel =
			(settings as File & { webkitRelativePath?: string }).webkitRelativePath ?? "";
		return { bytes, displayName: rel.split("/")[0] || "trail" };
	}

	throw new Error("no settings.trail or .srt found in folder");
}

interface IconProps {
	src?: string;
	placeholder: string;
	className?: string;
}

function Icon({ src, placeholder, className = "" }: IconProps): JSX.Element {
	if (src) {
		return (
			<img
				src={src}
				alt=""
				className={`size-4 shrink-0 rounded-sm object-cover ${className}`}
			/>
		);
	}
	return (
		<span className={`shrink-0 text-base leading-none ${className}`} aria-hidden>
			{placeholder}
		</span>
	);
}

interface RowProps {
	icon?: string;
	placeholder: string;
	label: string;
	hint?: string;
	active?: boolean;
	onSelect: () => void;
	onRemove?: () => void;
	disabled?: boolean;
}

function Row({
	icon,
	placeholder,
	label,
	hint,
	active,
	onSelect,
	onRemove,
	disabled,
}: RowProps): JSX.Element {
	return (
		<button
			type="button"
			role="menuitem"
			onClick={onSelect}
			disabled={disabled}
			className={`group flex w-full items-center gap-2 rounded-md border-0 bg-transparent px-2 py-1.5 text-left text-xs transition disabled:cursor-not-allowed disabled:opacity-50 ${
				active
					? "bg-emerald-500/10 text-emerald-200"
					: "text-zinc-200 hover:bg-zinc-800"
			}`}
		>
			<Icon src={icon} placeholder={placeholder} />
			<span className="min-w-0 flex-1 truncate">{label}</span>
			{hint && <span className="text-[10px] text-zinc-500">{hint}</span>}
			{onRemove && (
				<span
					role="button"
					tabIndex={-1}
					aria-label="Remove from saved trails"
					onClick={(e) => {
						e.stopPropagation();
						onRemove();
					}}
					className="ml-1 hidden h-5 w-5 items-center justify-center rounded text-sm text-zinc-500 hover:bg-zinc-700 hover:text-zinc-200 group-hover:flex"
				>
					×
				</span>
			)}
		</button>
	);
}

function SectionLabel({ children }: { children: string }): JSX.Element {
	return (
		<div className="px-2 pb-1 pt-2 text-[10px] font-semibold uppercase tracking-wider text-zinc-500">
			{children}
		</div>
	);
}

export function TrailMenu(): JSX.Element {
	const { identity, setIdentity, savedTrails, setSavedTrails } = useApp();

	const [open, setOpen] = useState(false);
	const [hover, setHover] = useState(false);
	const [error, setError] = useState<string | null>(null);
	const [busy, setBusy] = useState(false);
	const [presets, setPresets] = useState<TrailManifestEntry[]>([]);

	const wrapRef = useRef<HTMLDivElement | null>(null);
	const fileRef = useRef<HTMLInputElement | null>(null);

	// Load the bundled-trail manifest once. If it fails (offline /
	// missing file) the dropdown still works — the Presets section
	// just renders empty.
	useEffect(() => {
		let alive = true;
		fetchManifest()
			.then((m) => {
				if (alive) setPresets(m);
			})
			.catch((e) => {
				console.warn("[trail-menu] manifest fetch failed", e);
			});
		return () => {
			alive = false;
		};
	}, []);

	useEffect(() => {
		if (!open) return;
		const onClick = (e: MouseEvent): void => {
			if (wrapRef.current && !wrapRef.current.contains(e.target as Node)) {
				setOpen(false);
			}
		};
		const onKey = (e: KeyboardEvent): void => {
			if (e.key === "Escape") setOpen(false);
		};
		document.addEventListener("mousedown", onClick);
		document.addEventListener("keydown", onKey);
		return () => {
			document.removeEventListener("mousedown", onClick);
			document.removeEventListener("keydown", onKey);
		};
	}, [open]);

	// Identifying the active row when the user is on a preset / saved
	// trail. We compare by display name because that's what Identity
	// stores; collisions between a preset and a saved upload of the
	// same name are vanishingly unlikely and harmless if they happen.
	const activeName = identity.trail?.name ?? null;

	function applyTrail(name: string, bytes: Uint8Array, iconDataUrl?: string): void {
		const b64 = bytesToBase64(bytes);
		const trail = {
			name,
			b64,
			...(iconDataUrl ? { iconDataUrl } : {}),
		};
		setIdentity({ ...identity, trail });
	}

	async function pickPreset(entry: TrailManifestEntry): Promise<void> {
		setError(null);
		setBusy(true);
		try {
			const { bytes, iconDataUrl } = await fetchPresetAsSrt(entry);
			// Validate end-to-end (cheap; catches a busted bundle early).
			parseSrt(bytes);
			applyTrail(entry.displayName, bytes, iconDataUrl);
			setOpen(false);
		} catch (e) {
			setError(e instanceof Error ? e.message : "could not load preset");
		} finally {
			setBusy(false);
		}
	}

	function pickSaved(t: SavedTrail): void {
		setError(null);
		// Skip the bytes round-trip — savedTrails already store the
		// validated base64 blob.
		setIdentity({
			...identity,
			trail: {
				name: t.name,
				b64: t.b64,
				...(t.iconDataUrl ? { iconDataUrl: t.iconDataUrl } : {}),
			},
		});
		setOpen(false);
	}

	function removeSaved(id: string): void {
		setSavedTrails(removeSavedTrail(savedTrails, id));
	}

	function handleClear(): void {
		setError(null);
		setIdentity({ ...identity, trail: null });
		setOpen(false);
	}

	async function handleFiles(fileList: FileList): Promise<void> {
		setError(null);
		try {
			const { bytes, displayName } = await readTrailFromFolder(fileList);
			const payload = parseSrt(bytes); // validates + extracts icon
			const iconDataUrl = payload.icon ? bytesToPngDataUrl(payload.icon) : undefined;
			applyTrail(displayName, bytes, iconDataUrl);
			const b64 = bytesToBase64(bytes);
			setSavedTrails(
				addSavedTrail(savedTrails, {
					name: displayName,
					b64,
					...(iconDataUrl ? { iconDataUrl } : {}),
				}),
			);
			setOpen(false);
		} catch (e) {
			setError(e instanceof Error ? e.message : "could not read trail");
		}
	}

	// Fixed width — keeps the pill from jumping when the active trail
	// name changes length and forces ellipsis on long names instead of
	// reflowing the surrounding header.
	const pillBase =
		"flex h-9 w-44 items-center gap-1.5 rounded-lg border-0 bg-transparent px-3 text-xs font-medium transition";
	const pillState = error
		? "text-red-300 hover:bg-zinc-800"
		: hover
			? "bg-amber-400/10 text-amber-200"
			: identity.trail
				? "text-emerald-300 hover:bg-zinc-800"
				: "text-zinc-400 hover:bg-zinc-800";

	const showUploadHover = hover && !open;

	return (
		<div ref={wrapRef} className="relative">
			<input
				ref={fileRef}
				type="file"
				className="hidden"
				// Folder picker (the .srt files in
				// CEngineStorage/.../Local/ are empty stubs; the real
				// content lives unzipped under
				// Steam/userdata/.../remote/trails/<name>/).
				{...({
					webkitdirectory: "",
					directory: "",
				} as React.InputHTMLAttributes<HTMLInputElement>)}
				onChange={(e) => {
					const fs = e.target.files;
					if (fs && fs.length > 0) void handleFiles(fs);
					// Reset so picking the same folder again still fires onChange.
					e.target.value = "";
				}}
			/>

			<button
				type="button"
				onClick={() => setOpen((v) => !v)}
				onMouseEnter={() => setHover(true)}
				onMouseLeave={() => setHover(false)}
				onContextMenu={(e) => {
					e.preventDefault();
					handleClear();
				}}
				aria-haspopup="menu"
				aria-expanded={open}
				title={
					error
						? `Trail import failed: ${error}`
						: identity.trail
							? `${identity.trail.name} — click to swap. Right-click to clear.`
							: "Pick a preset or upload a trail folder"
				}
				className={`${pillBase} ${pillState}`}
			>
				{showUploadHover ? (
					<>
						<UploadIcon className="size-3.5" />
						<span className="min-w-0 flex-1 truncate text-left">Upload</span>
					</>
				) : identity.trail ? (
					<>
						<Icon
							src={identity.trail.iconDataUrl}
							placeholder="✓"
							className={identity.trail.iconDataUrl ? "" : "text-base"}
						/>
						<span className="min-w-0 flex-1 truncate text-left">
							{identity.trail.name}
						</span>
					</>
				) : (
					<>
						<span className="text-base leading-none" aria-hidden>
							✦
						</span>
						<span className="min-w-0 flex-1 truncate text-left">Trail</span>
					</>
				)}
				<ChevronDown
					className={`size-3 shrink-0 text-zinc-500 transition ${open ? "rotate-180" : ""}`}
				/>
			</button>

			{open && (
				<div
					role="menu"
					className="absolute right-0 z-30 mt-2 w-72 rounded-xl border border-zinc-800 bg-zinc-950/95 p-1.5 shadow-xl shadow-black/40 backdrop-blur"
				>
					{busy && (
						<div className="px-2 py-2 text-[11px] text-zinc-500">Loading…</div>
					)}

					<SectionLabel>Presets</SectionLabel>
					{presets.length === 0 ? (
						<div className="px-2 py-1.5 text-[11px] text-zinc-500">
							No presets bundled.
						</div>
					) : (
						presets.map((p) => (
							<Row
								key={p.id}
								icon={p.icon ? `${BASE}trails/${p.id}/${p.icon}` : undefined}
								placeholder="✦"
								label={p.displayName}
								active={activeName === p.displayName}
								disabled={busy}
								onSelect={() => void pickPreset(p)}
							/>
						))
					)}

					{savedTrails.length > 0 && (
						<>
							<SectionLabel>Your trails</SectionLabel>
							{savedTrails.map((t) => (
								<Row
									key={t.id}
									icon={t.iconDataUrl}
									placeholder="✦"
									label={t.name}
									active={activeName === t.name}
									onSelect={() => pickSaved(t)}
									onRemove={() => removeSaved(t.id)}
								/>
							))}
						</>
					)}

					<SectionLabel>Add or remove</SectionLabel>
					<Row
						placeholder="↑"
						label="Upload from folder…"
						onSelect={() => fileRef.current?.click()}
					/>
					{identity.trail && (
						<Row placeholder="×" label="Clear current trail" onSelect={handleClear} />
					)}

					{error && (
						<div className="mt-1 rounded-md border border-red-500/40 bg-red-500/10 px-2 py-1.5 text-[11px] text-red-300">
							{error}
						</div>
					)}
				</div>
			)}
		</div>
	);
}

// Small inline icons — kept here so the menu doesn't pull in an icon
// library for two glyphs.
function UploadIcon({ className = "" }: { className?: string }): JSX.Element {
	return (
		<svg
			viewBox="0 0 16 16"
			fill="none"
			stroke="currentColor"
			strokeWidth="1.5"
			strokeLinecap="round"
			strokeLinejoin="round"
			aria-hidden
			className={className}
		>
			<path d="M8 11V3" />
			<path d="M5 6l3-3 3 3" />
			<path d="M3 13h10" />
		</svg>
	);
}

function ChevronDown({ className = "" }: { className?: string }): JSX.Element {
	return (
		<svg
			viewBox="0 0 16 16"
			fill="none"
			stroke="currentColor"
			strokeWidth="1.5"
			strokeLinecap="round"
			strokeLinejoin="round"
			aria-hidden
			className={className}
		>
			<path d="M4 6l4 4 4-4" />
		</svg>
	);
}

