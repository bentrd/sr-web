# SR-Web Implementation Plan

Browser port of [SR-cpp](https://github.com/rbit-sr/SR-cpp) (a SpeedRunners reimplementation in C++) with ghost-style multiplayer (each client streams its own state at 30Hz; remote players rendered at 50% opacity).

> **Before starting any task, read [AGENTS.md](./AGENTS.md).** It contains the non-obvious project context (time units, legacy GL, file formats, etc.) you will get wrong otherwise.

---

## How to use this file

- Each task is a checkbox. Tick it (`- [x]`) when complete and committed.
- Each phase has an **exit gate** — the phase isn't done until that box is ticked.
- Multiple agents can work in parallel. See **Parallelization** at the bottom for safe concurrency.
- If you change scope, edit this file in the same commit.

---

## Open decisions (resolved)

- [x] **Map redistribution**: `.sr` files gitignored; each dev runs `collect-maps` locally. Manifest committed.
- [x] **Soft player-count warning threshold**: warn in client when room size > **12**.
- [x] **SR-cpp inclusion**: vendored at `game/src/SR cpp/` (changed from submodule — Phase 4a/b/d/e require ongoing source modifications).
- [x] **Identity persistence**: name + color persisted via localStorage.
- [x] **Server hosting target**: Fly.io for the demo deploy.

---

## Phase 0 — Bootstrap

- [x] Initialize Bun workspace at repo root (`package.json` with `workspaces: ["apps/*", "packages/*"]`)
- [x] Add SR-cpp as git submodule at `game/upstream`
- [x] Scaffold `apps/web` (Vite + React + TypeScript)
- [x] Scaffold `apps/server` (Bun + TypeScript, `Bun.serve` skeleton)
- [x] Scaffold `packages/protocol` (shared TS types, exported)
- [x] Add root `bun dev` script that runs web + server concurrently
- [x] Add `.editorconfig`, `.gitignore`, `tsconfig.base.json`
- [x] **Exit gate**: `bun dev` opens a React page at `:5173` that connects to the WS server at `:4000` and exchanges a ping/pong

---

## Phase 1 — Map collection

- [x] Write `scripts/collect-maps.ts` — scans `~/Library/Application Support/SpeedRunners/CEngineStorage/AllPlayers/Subscribed`
- [x] Match 4 target maps by filename suffix (case-insensitive):
  - `Pitfall.sr`
  - `Genetics.sr`
  - `Grapple Circuit.sr`
  - `Oasis - Abyss.sr`
- [x] Copy matched files to `game/assets/maps/{pitfall,genetics,grapple_circuit,oasis_abyss}.sr`
- [x] Generate `game/assets/maps/manifest.json`: `[{ id, displayName, file }]`
- [x] Commit `manifest.json`; gitignore the `.sr` files (per open decision)
- [x] Add `bun run collect-maps` to root `package.json`
- [x] **Exit gate**: `bun run collect-maps` produces 4 files + manifest on a fresh clone

---

## Phase 2 — Lobby UI

*Can run in parallel with Phase 3 and Phase 4a/4b after Phase 0 + 1 are done.*

- [x] Set up routing (`/` home, `/r/:code` room) — `react-router-dom`
- [x] Home page: name input + `<input type="color">` + [Create] [Join code____]
- [x] Map picker (hardcoded 4 maps in `lobby/maps.ts`; manifest is the source of truth — switch to runtime fetch when adding more)
- [x] Persist name + color to localStorage (`useApp().identity`)
- [x] WS client wrapper with auto-reconnect, message typing imported from `packages/protocol`
- [x] Create-room flow: pick map → WS `create_room` → navigate to `/r/:code` (driven by `room_state`)
- [x] Join-room flow: enter code → WS `join_room` → navigate to `/r/:code`
- [x] Room view: player list (name + color swatch), map name, host indicator, "you" tag
- [x] [Start] button (host only), [Leave] button
- [x] Soft warning banner when room player count > 12
- [x] Error states: server unreachable, room not found, room already started
- [x] **Exit gate**: typecheck + prod build green; protocol verified end-to-end with 2 simulated WS clients. *Final human-eyes browser test still recommended.*

---

## Phase 3 — Server

*Can run in parallel with Phase 2 and Phase 4 after Phase 0.*

- [x] `Bun.serve({ websocket })` skeleton with typed message routing
- [x] In-memory `Map<code, Room>` room store (`apps/server/src/rooms.ts`)
- [x] 5-char Crockford base32 code generator (no I, L, O, U) with collision check (`apps/server/src/codes.ts`) — also includes `normaliseCode()` for lenient user input
- [x] Message handlers (C→S): `create_room`, `join_room`, `leave_room`, `start_game`, `snapshot`
- [x] Broadcast handlers (S→C): `room_state`, `welcome`, `player_joined`, `player_left`, `game_started`, `snapshot`, `error`
- [x] Snapshot fanout: relay to all room members except sender (no inspection of body)
- [x] Disconnect handling: 30s grace period for reconnect with same player id
- [x] Idle-room GC (delete after 10min with no activity)
- [x] No hard player cap (warning is client-side, see Phase 2)
- [x] **Exit gate**: integration test passes for create / join / snapshot fanout / leave / start / not-host / room-not-found / room-already-started

---

## Phase 4 — Patch SR-cpp

*The largest phase. Sub-phases 4a + 4b can run in parallel; 4c, 4d, 4e are mostly sequential.*

### 4a. CMake build alongside `.sln`

- [x] Write `game/CMakeLists.txt` enumerating all source dirs from `SR cpp/`
- [x] Conditional dependencies: GLEW + native GLFW on desktop; Emscripten ports (`-sUSE_GLFW=3`, `-sUSE_ZLIB=1`) on web
- [x] CMake **configure** step succeeds on macOS (finds GLFW 3.4 + GLEW 2.3 via brew/pkg-config)
- [x] Verify desktop CMake build produces a working binary (1.6MB Mach-O arm64 produced on macOS)
- [x] **Exit gate**: `cmake --build build-desktop` produces `sr_desktop` binary

**Source patches applied during Phase 4a** (vendored copy, not pushed upstream):

1. `command/string_util.cpp` — Added `#include <charconv>`. Replaced `std::from_chars` for floats with `std::stof` (float `from_chars` requires macOS 26+ in libc++).
2. `emulation/math.h` → renamed to `emulation/sr_math.h` (shadowed libc++ `<math.h>`). All includes updated.
3. `instance.cpp` — Added missing `#include <iostream>` and `#include <thread>`.
4. `emulation/file_util.h` — Changed `#include "zlib/zlib.h"` to `#include <zlib.h>` (use system zlib instead of vendored `.c` files which fail to compile on macOS).

**Resolved decision**: SR-cpp **vendored** at `game/src/SR cpp/` (option a). Submodule dropped because Phase 4b/4d/4e will rewrite large chunks.

### 4b. Modern GL rewrite of `draw_util.cpp`

> **This is the biggest single task in the project.** WebGL has zero support for the immediate mode the original uses. Required for both desktop and web targets after the rewrite.

- [x] Audit all `glBegin`/`glEnd`/`glVertex*`/`glColor*` call sites
- [x] Add minimal shader sources (vertex + fragment): takes pos + color uniform + alpha (GL 3.3 core on desktop, GLSL ES 3.0 on web — `#ifdef __EMSCRIPTEN__`)
- [x] Set up VBO/VAO helpers + a single shader program (anonymous namespace `gl_state` in draw_util.cpp)
- [x] Rewrite `draw_triangle`, `draw_rectangle` (both overloads), `draw_line` (plus `_a` alpha variants for ghosts)
- [x] Rewrite `draw_tile`, `draw_tile_layer`
- [x] Rewrite `draw_player`, `draw_grapple`
- [x] Rewrite remaining draw functions (`draw_player_start`, `draw_super_boost_volume`, `draw_boost_section`, `draw_obstacle`, `draw_state`, `draw_actor_controller`, `draw_right_pot_map`, `draw_left_pot_map`)
- [x] **Fix the `bounds.max_x` → `max_y` bug** (`draw_rectangle(aabb)` now delegates to the corner-vector overload with correct min/max)
- [x] GLFW context hints for 3.3 core + forward-compat (macOS); `glewInit()` moved after `glfwMakeContextCurrent`
- [x] `draw::set_viewport()` replaces the old `glMatrixMode/glOrtho` calls in `instance.cpp`
- [x] **Exit gate**: desktop binary boots into a GLFW window with shader-based rendering, no segfault during a 3s smoke run; visual side-by-side parity check deferred to first end-to-end run with a ghost peer

### 4c. Tickable main loop

- [x] Extract `instance::tick_frame()` from `instance::run()`
- [x] Desktop entry: `run(map_path)` calls `while(!should_close()) tick_frame();`
- [x] `limit_rate(300)` busy-wait gated `#ifndef __EMSCRIPTEN__` — web build will be driven at monitor refresh by `emscripten_set_main_loop_arg(0, ...)` instead
- [x] **Exit gate**: desktop behavior unchanged from before refactor (smoke test still boots and renders cleanly)

### 4d. Ghost players (render-only)

- [x] `net::ghost_state` struct (`game/src/SR cpp/network/ghost_state.h`): name, color rgb, position, velocity, size, facing, anim, grapple state
- [x] `net::ghost_manager` (owned by `playground`, exposed via `m_ghosts`): mutex-protected `unordered_map<string, ghost_state>` with `push/set_identity/remove/clear/snapshot`
- [x] `draw::draw_ghost(...)` renders body + grapple line + attach marker at 50% alpha
- [x] `net::local_identity` struct + `draw::set_local_player_color()` — JS-set color overrides hardcoded red on the local player rectangle
- [x] JS-callable C ABI in `network/sr_api.cpp` (all `extern "C"`):
  - [x] `sr_set_local_identity(name, r, g, b)`
  - [x] `sr_load_map(path)` — also clears ghost map
  - [x] `sr_push_ghost(id, pos, vel, facing, anim, grapple…)`
  - [x] `sr_set_ghost_identity(id, name, r, g, b)`
  - [x] `sr_remove_ghost(id)`
  - [x] `sr_get_local_snapshot(out_buf, buf_size) -> size_t` (40-byte fixed layout)
  - [x] `sr_get_player_screen_pos(id, out_x, out_y) -> int` (id="" or NULL = local)
- [x] Snapshot wire layout mirrored in `packages/protocol/src/index.ts` (`SNAPSHOT_BYTES`, `SNAPSHOT_OFFSETS`) so JS encoder/decoder shares one schema
- [x] Render path: `playground::draw` calls `set_local_player_color` (if identity set), `draw_state`, then iterates `m_ghosts.snapshot()` and `draw_ghost`s each at 50% alpha
- [x] **Exit gate**: desktop binary still boots cleanly with the new wiring; end-to-end ghost render gets exercised in Phase 6 once JS is calling the C ABI

### 4e. Map loading via argument

- [x] Remove hardcoded `INIT_LOAD_LEVEL` from `playground.h:4`
- [x] `playground::load(map_path)` loads + inits; `instance::run(map_path)` is the entry point
- [x] `main(argc, argv)` accepts CLI map arg; defaults to `game/assets/maps/pitfall.sr`
- [x] **Exit gate**: `./sr_desktop game/assets/maps/<id>.sr` works for any of the 4 maps

---

## Phase 5 — Emscripten build

*Depends on Phase 4 complete.*

- [x] CMakeLists EMSCRIPTEN branch (no separate toolchain file needed — emcmake injects the toolchain)
- [x] `<GL/glew.h>` swapped for `<GLES3/gl3.h>` via `#ifdef __EMSCRIPTEN__` in draw_util.h
- [x] `glewInit()` gated `#ifndef __EMSCRIPTEN__` in instance.cpp (Emscripten's `-sFULL_ES3=1` resolves GL pointers itself)
- [x] `::clone<T>` calls renamed to `emu::clone<T>` (collided with musl `<sched.h>` `clone` on Emscripten)
- [x] All Emscripten flags set in `target_link_options`: USE_GLFW=3, FULL_ES3=1, MIN/MAX_WEBGL_VERSION=2, USE_ZLIB=1, ALLOW_MEMORY_GROWTH=1, MODULARIZE=1, EXPORT_NAME=createSrModule, ENVIRONMENT=web, plus the EXPORTED_FUNCTIONS / EXPORTED_RUNTIME_METHODS lists and `--preload-file ...assets/maps@/maps`
- [x] `game/platform/web_main.cpp` registers the active instance, loads pitfall by default, and drives `instance::tick_frame` via `emscripten_set_main_loop_arg(fps=0, simulate_infinite_loop=1)`
- [x] Output drops directly into `apps/web/public/{sr.js,sr.wasm,sr.data}` via `RUNTIME_OUTPUT_DIRECTORY`
- [x] `bun run build:wasm` → `scripts/build-wasm.sh` (also pins `EMSDK_PYTHON` because brew's python sniff defaults to system 3.9)
- [x] **Exit gate**: `bun run build:wasm` produces sr.js (127KB) + sr.wasm (562KB) + sr.data (87KB, all 4 maps preloaded under /maps); all 7 sr_* exports present in sr.js. End-to-end browser play deferred to Phase 6.

---

## Phase 6 — Wire it together

*Depends on Phases 2, 3, 5.*

- [x] React `<Game>` component: mount canvas, await Module ready
- [x] On mount: `sr_set_local_identity(name, r, g, b)` + `sr_load_map(/maps/<id>.sr)`
- [x] Set up 30Hz interval (`setInterval` or RAF-based clock): `sr_get_local_snapshot()` → WS `snapshot`
- [x] On WS `snapshot` from peer: `sr_set_ghost_identity` (if first time) + `sr_push_ghost(...)`
- [x] On WS `player_left`: `sr_remove_ghost(id)`
- [x] Per-`requestAnimationFrame` name overlay: for each ghost id, `sr_get_player_screen_pos`, render `<div className="player-label">name</div>` with `transform: translate(x, y)`
- [x] Also overlay local player's own name above their character
- [x] Disable browser shortcuts that conflict with game keys (focus-locked canvas, preventDefault on space/arrows)
- [x] **Exit gate**: end-to-end browser run shows the level + colored player + name label; verified via Chrome DevTools MCP at /r/CODE → Start game. Caught and fixed three Emscripten-only bugs along the way: (1) anchor `sr.data` / `sr.wasm` via `Module.locateFile = '/' + path` so they don't 404 under the SPA fallback at /r/CODE, (2) `read_string` was using `istreambuf_iterator + stream.ignore()` which over-skipped on Emscripten libc++; switched to `stream.read()`, (3) skip `glfwWindowHint(CONTEXT_VERSION...)` and `start_command_loop()` (`std::thread`) under `__EMSCRIPTEN__`. Build also needs `-fexceptions` because the level loader throws.

---

## Phase 7 — Polish + deploy

- [x] Production builds for web + server (`bun run build` → 175KB JS + 9.5KB server bundle)
- [x] Env vars for WS URL (dev vs prod) — `VITE_WS_URL`, `apps/web/.env.example` documents the shape
- [x] Error states: server unreachable, room not found, WASM load failure (Game.tsx try/catch + Room.tsx `lastError` + WsClient `closed` status renders in the footer)
- [x] Reconnect UX: WsClient already has exponential-backoff auto-reconnect; the "server: closed" footer surfaces it
- [x] Deploy artifacts: `apps/server/Dockerfile` + `apps/server/fly.toml` for Fly.io. Static frontend just needs `VITE_WS_URL=wss://...` baked in at build time and any host that serves `application/wasm` (Vercel/Cloudflare both do).
- [x] **Frontend deploy**: `bash scripts/deploy-pages.sh` publishes to `gh-pages` branch → https://bentrd.github.io/sr-web/
- [x] **Server deploy**: `flyctl deploy --config apps/server/fly.toml --dockerfile apps/server/Dockerfile --remote-only` (single-machine, pinned to `cdg`)
- [x] Update README with run + deploy instructions
- [x] **Exit gate**: frontend live at https://bentrd.github.io/sr-web/, server live at wss://sr-web-server.fly.dev/ws — shareable URL works end-to-end

---

## Parallelization

Safe parallel work after Phase 0 + 1 are done:

```
Phase 2 (Lobby UI)        ─┐
Phase 3 (Server)          ─┤── parallel
Phase 4a (CMake)          ─┤
Phase 4b (Modern GL)      ─┘

Phase 4c (tick refactor)  ── after 4a
Phase 4d (ghosts)         ── after 4b + 4c
Phase 4e (map arg)        ── after 4a (can overlap with 4d)
Phase 5 (Emscripten)      ── after all of Phase 4
Phase 6 (wire-up)         ── after 2, 3, 5
Phase 7 (deploy)          ── after 6
```

When picking up a task: tick the box in a commit *before* starting (claim it), or use a `WIP:` prefix in the commit message. Either way, communicate.

---

## Phase 8 — UX polish (post-launch)

*Independent of Phase 7. Each item is its own commit; no cross-task ordering.*

### 8a. Public lobbies + room metadata

- [x] Extend `create_room` with `displayName`, `maxPlayers` (-1 = unlimited), `public` flag (`packages/protocol/src/index.ts`)
- [x] `RoomState` carries the same fields; server enforces capacity with `room_full` error
- [x] `set_room_visibility` C→S message; `subscribe_public_rooms` / `unsubscribe_public_rooms` + `public_rooms_list` S→C
- [x] Server-side `Map<code, Room>` tracks `isPublic`; broadcast subscribers on join/leave/start/gc/visibility-change
- [x] Home page renders a live public-lobby browser with map filter + capacity bars
- [x] Room header has a Public/Private toggle (host only)
- [x] **Exit gate**: typecheck green; manual round-trip from second tab shows public room appearing in the list

### 8b. Tailwind v4 redesign

- [x] Add `tailwindcss` + `@tailwindcss/vite` to `apps/web` (utilities-only, preflight skipped)
- [x] Wrap legacy `styles.css` inside `@layer base` (per CSS Cascade Layers spec, unlayered always wins) — see `apps/web/src/tailwind.css`
- [x] Rewrite `Home.tsx` with utility classes, full-page width, muted amber primary buttons
- [x] Visibility toggle as segmented control (Private/Public) with proper active-state separation
- [x] **Exit gate**: prod build green; lobby looks intentional, no clipped layouts

### 8c. Game options modal

- [x] C++: `visuals_config.{h,cpp}` holding background / walls body & stripe / wallclimb stripe / grapple stripe / grapple cord / grapple head color + size
- [x] `instance::draw()` reads bg color from `visuals_config` per frame; `draw_util.cpp` reads body / stripe / cord / head colors and head size from the config
- [x] 7 new `sr_set_*` ABI exports in `network/sr_api.cpp` + `EXPORTED_FUNCTIONS` in `CMakeLists.txt`
- [x] JS: `state/visuals.ts` (types + defaults + localStorage), `AppState` extended with `visuals` + `setVisuals`
- [x] `OptionsModal.tsx` (mirrors `ControlsModal` pattern) with a 16M color picker per slot + slider for grapple-head size + FPS
- [x] Move FPS row out of `ControlsModal` into `OptionsModal`
- [x] `Game.tsx` cwraps the 7 setters and applies them in a `useEffect([visuals, status])`
- [x] `Room.tsx` adds an Options button next to the Controls button in the game-bar
- [x] **Exit gate**: WASM rebuild succeeds (sr.js / sr.wasm have all 7 visual exports); typecheck + prod build green. Live-toggle visual confirmation deferred to next browser run.

### 8d. Quicksave + boost colors + speedometer

- [x] C++: `local_save_state` in `network/sr_api.cpp` captures position / velocity / boost; `sr_save_state` + `sr_load_state` ABI exports
- [x] C++: `boost_section_{rgba}` + `boost_pickup_{rgba}` in `visuals_config.h`; `draw_super_boost_volume` + `draw_boost_section` use `draw_rectangle_a` with config alpha
- [x] C++: `sr_set_visual_boost_section(r,g,b,a)` + `sr_set_visual_boost_pickup(r,g,b,a)` ABI exports; both added to `EXPORTED_FUNCTIONS`
- [x] JS bindings: `save_state` (F5) + `load_state` (F9) added to `UI_ACTIONS` / `DEFAULT_BINDINGS` / `ACTION_LABELS`
- [x] JS visuals: `ColorRgba` + `SpeedometerMode` types; `boostSection` / `boostPickup` / `speedometer` in `Visuals` + `VISUAL_DEFAULTS`; `speedColor(s)` returns oklch threshold colors at 1400/1300/1200/900/750
- [x] `OptionsModal`: 2-column grid for color rows (RGB single col, RGBA col + alpha slider); 3-way segmented Speedometer toggle (Off/Self/All)
- [x] `Game.tsx`: cwraps `setVisualBoostSection` / `setVisualBoostPickup` / `saveState` / `loadState`; combined capture-phase keydown handler (reset + save + load); per-frame computes local + ghost speeds and renders `√(vx²+vy²)` overlay above each player
- [x] **Exit gate**: WASM rebuild succeeds (4 new exports verified in sr.js); typecheck + prod build green. Live keybind + speedometer confirmation deferred to next browser run.

---

### 8e. SR-style trails (workshop trail import)

*MVP: hardcoded Goldilocks trail (workshop ID 3230477673). Local player only. Renders behind the player so it doesn't obscure the body.*

- [x] **Asset pipeline**: `scripts/collect-trails.ts` copies `~/Library/Application Support/Steam/steamapps/common/SpeedRunners/SpeedRunners.app/Contents/Resources/WorkshopContent/3230477673/` into `apps/web/public/trails/goldilocks/` (settings.trail + PNGs + manifest.json). Add `bun run collect-trails` script. Wire into `scripts/deploy-pages.sh`.
- [x] **JS .trail parser**: `apps/web/src/game/trail/parseTrail.ts` — pure parser for the binary format (uint32 LE counts, uint8 length-prefixed strings, layer property pairs). Returns typed `TrailDefinition` with `name`, `author`, `layers[]`. Each layer carries `imageName`, `enabledMode`, `lifetime`, `color`, `opacity`, `size`, `taper`, `fadeOut`, `fadeOutSpeed`, `flipH`, `flipV`, `forceRightSideUp`, `offsetVector`, `invertOffset`.
- [x] **C++ trail subsystem**: `game/src/SR cpp/drawing/trail.{h,cpp}` — own shader (sampler2D + vertex color), own VAO/VBO, per-layer ring buffer of (pos, vel, age) samples; ribbon mesh as triangle strips with normal-offset vertices and stretched UV. Hook `playground::update` (sample) and `playground::draw` (draw before `draw_state` so it's behind the player).
- [x] **C ABI** in `network/sr_api.cpp`: `sr_trail_clear()`, `sr_trail_register_image(name, w, h, rgba_ptr, byte_count)`, `sr_trail_add_layer(...)`. Add to `EXPORTED_FUNCTIONS` in `game/CMakeLists.txt`.
- [x] **JS wiring**: `apps/web/src/game/trail/loadTrail.ts` fetches manifest + .trail + PNGs (decode via Image+canvas), pushes RGBA bytes via `sr_trail_register_image`, then `sr_trail_add_layer` per parsed layer. Wire into `Game.tsx` after `sr_load_map`.
- [x] **Exit gate**: `bun run collect-trails` succeeds, `bash scripts/build-wasm.sh` rebuilds WASM with the 3 new exports verified in sr.js (`_sr_trail_clear`, `_sr_trail_register_image`, `_sr_trail_add_layer`), `bunx tsc --noEmit` + `bun run build` both green, dist bundle contains `trails/goldilocks/{settings.trail,13/14/16.png}` + `trails/manifest.json`. Live browser test deferred to next play session.

*v2: user-picked `.srt` import, per-player tracks (local + each ghost), ghost trails at half opacity, "show other players' trails" toggle.*

- [x] **Trail subsystem refactor (C++)**: `trail.{h,cpp}` keyed by `track_id` (`""` = local, peer id = ghost) via `unordered_map<string, track>`. Per-track `opacity_mul` (0.5 for ghosts) multiplied into vertex alpha at draw; per-track `visible` flag for the toggle. New helpers `clear_track`, `set_track_opacity`, `set_track_visible`. ALWAYS-layer strip-break grace tightened to 0.001s (matches SUPERSPEED).
- [x] **C ABI extensions**: `sr_trail_clear_track`, `sr_trail_set_track_opacity`, `sr_trail_set_track_visible`; existing `sr_trail_register_image` / `sr_trail_add_layer` gained leading `track_id` arg. Ghost recording hooked into `sr_push_ghost`; cleanup in `sr_remove_ghost`.
- [x] **Browser .srt parser**: `apps/web/src/game/trail/parseSrt.ts` (fflate-based zip extractor) + `bytesToBase64` / `base64ToBytes` helpers. Settings.trail + every PNG matched by basename.
- [x] **Identity + visuals**: `Identity.trail` field (persisted to localStorage as `{ name, b64 }`), `visuals.showGhostTrails` flag (default on, persisted).
- [x] **Lobby UI**: "Trail" pill button in `Home.tsx` opens hidden `<input type="file" accept=".srt">`; right-click clears. "Show other players' trails" Off/On toggle in `OptionsModal.tsx`.
- [x] **Server relay**: `PROTOCOL_VERSION` 3→4. New `trail_share` WS message (in/out). `ServerPlayer.trailB64` cache + `setPlayerTrail` / `peerTrails`. Server validates ≤384KB body, broadcasts to room (excluding sender), and replays cached blobs on `join_room` so late joiners see existing trails.
- [x] **Game.tsx wiring**: boot loads `identity.trail` (or default) into track `""`; broadcasts own `trail_share` once WASM ready (and on swap); inbound `trail_share` → `loadTrailFromBytes(mod, peerId, …)` + `setTrackOpacity(0.5)` + `setTrackVisible(showGhostTrails)`; `player_left` → `clearTrack`; effect re-applies visibility to all loaded peer tracks when toggle flips.
- [x] **Exit gate**: `bun run typecheck` + `bun run build:wasm` green; reviewer pass APPROVED with no critical/major issues. End-to-end browser test deferred to next play session.

### 8f. Trail dropdown (UX polish)

*Replaces the trail pill's "click = file picker" affordance with a real dropdown menu: presets (Goldilocks + Orange Superspeed), the user's recently-uploaded trails (cap 8 FIFO in localStorage), an Upload action that still opens the folder picker, and a Clear action. Hover swaps the pill label to "Upload" with an upload glyph.*

- [x] **Asset bundling**: extend `scripts/collect-trails.ts` to support both workshop and userdata sources; copy the per-trail `icon` blob; bundle `goldilocks` (workshop ID 3230477673) + `orange-superspeed` (userdata `Orange Superspeed Trail`)
- [x] **Manifest schema**: add `icon` field to manifest entries (`apps/web/public/trails/manifest.json` + `TrailManifestEntry` in `loadTrail.ts`)
- [x] **Parser extension**: add `icon: Uint8Array | null` to `SrtPayload` in `parseSrt.ts`; extract entries named exactly `icon` (no extension) alongside the PNGs
- [x] **Identity icon**: extend `Identity.trail` with optional `iconDataUrl` so the pill can show the icon inline; persist with the rest of the trail blob
- [x] **Saved trails state**: new `apps/web/src/state/savedTrails.ts` (`SavedTrail` type + `loadSavedTrails` + `addSavedTrail` capped at 8 FIFO via `sr-web.saved-trails`); thread `savedTrails` + `addSavedTrail` through `AppState`
- [x] **TrailMenu component**: new `apps/web/src/lobby/TrailMenu.tsx` — pill defaulting to `✦ Trail` (or current trail icon + name); hover swaps to `↑ Upload`; click opens dropdown with Presets section + Your trails section + Upload + Clear; outside-click and Esc close it
- [x] **Home.tsx refactor**: replace inline trail block with `<TrailMenu />`; lift `handleTrailFiles` into the component
- [x] **Exit gate**: `bun run typecheck` + `bun run build` both green (protocol + server + web); manual lobby visual confirmation deferred to next browser run

---

## Non-goals

- Server-authoritative simulation, anti-cheat
- Shared-camera "trailing player dies" mechanic (by design — see AGENTS.md)
- Direct player↔player interaction (grapples don't pull ghosts; items don't hit ghosts)
- Matchmaking, accounts, persistence, leaderboards
- Mobile / touch controls
- Spectator mode, voice chat
