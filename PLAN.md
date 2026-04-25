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
- [x] **SR-cpp inclusion**: git submodule at `game/upstream`.
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

- [ ] Set up routing (`/` home, `/r/:code` room)
- [ ] Home page: name input + `<input type="color">` + [Create] [Join code____]
- [ ] Map picker component (reads `manifest.json`)
- [ ] Persist name + color to localStorage
- [ ] WS client wrapper with auto-reconnect, message typing imported from `packages/protocol`
- [ ] Create-room flow: pick map → WS `create_room` → navigate to `/r/:code`
- [ ] Join-room flow: enter code → WS `join_room` → navigate to `/r/:code`
- [ ] Room view: player list (name + color swatch), map name, host indicator
- [ ] [Start] button (host only), [Leave] button
- [ ] Soft warning banner when room player count > 12
- [ ] Error states: server unreachable, room not found, room already started
- [ ] **Exit gate**: 2 browser tabs can create + join a room and see each other's name/color in the player list

---

## Phase 3 — Server

*Can run in parallel with Phase 2 and Phase 4 after Phase 0.*

- [ ] `Bun.serve({ websocket })` skeleton with typed message routing
- [ ] In-memory `Map<code, Room>` room store
- [ ] 5-char Crockford base32 code generator (no I, L, O, U) with collision check
- [ ] Message handlers (C→S): `create_room`, `join_room`, `leave_room`, `start_game`, `snapshot`
- [ ] Broadcast handlers (S→C): `room_state`, `player_joined`, `player_left`, `game_started`, `snapshot`
- [ ] Snapshot fanout: relay to all room members except sender (no inspection of body)
- [ ] Disconnect handling: 30s grace period for reconnect with same player id
- [ ] Idle-room GC (delete after 10min with no members)
- [ ] No hard player cap (warning is client-side, see Phase 2)
- [ ] **Exit gate**: integration test (or manual 4-tab test) passes for create / 4 joins / snapshot fanout / leave / GC

---

## Phase 4 — Patch SR-cpp

*The largest phase. Sub-phases 4a + 4b can run in parallel; 4c, 4d, 4e are mostly sequential.*

### 4a. CMake build alongside `.sln`

- [ ] Write `game/CMakeLists.txt` enumerating all source dirs from `SR cpp/`
- [ ] Conditional dependencies: GLEW + native GLFW on desktop; Emscripten ports (`-sUSE_GLFW=3`, `-sUSE_ZLIB=1`) on web
- [ ] Verify desktop CMake build produces a working binary identical in behavior to current `.sln` build
- [ ] **Exit gate**: `cmake --build build-desktop && ./sr_desktop` runs and matches existing `.sln` output

### 4b. Modern GL rewrite of `draw_util.cpp`

> **This is the biggest single task in the project.** WebGL has zero support for the immediate mode the original uses. Required for both desktop and web targets after the rewrite.

- [ ] Audit all `glBegin`/`glEnd`/`glVertex*`/`glColor*` call sites
- [ ] Add minimal shader sources (vertex + fragment): takes pos + color uniform + alpha
- [ ] Set up VBO/VAO helpers + a single shader program
- [ ] Rewrite `draw_triangle`, `draw_rectangle` (both overloads), `draw_line`
- [ ] Rewrite `draw_tile`, `draw_tile_layer`
- [ ] Rewrite `draw_player`, `draw_grapple`
- [ ] Rewrite remaining draw functions (`draw_player_start`, `draw_super_boost_volume`, `draw_boost_section`, `draw_obstacle`, etc — enumerate from `draw_util.h`)
- [ ] **Fix the `bounds.max_x` → `max_y` bug at `draw_util.cpp:25-26`** (in passing)
- [ ] Verify desktop visual parity with old build (side-by-side screenshots if possible)
- [ ] **Exit gate**: desktop binary renders identically using only modern GL

### 4c. Tickable main loop

- [ ] Extract `instance::tick_frame()` from `instance::run()`
- [ ] Desktop entry calls `while (!glfwWindowShouldClose(...)) inst.tick_frame();`
- [ ] Confirm the 300Hz sim accumulator + monitor-rate render decoupling is preserved
- [ ] **Exit gate**: desktop behavior unchanged from before refactor

### 4d. Ghost players (render-only)

- [ ] Define `ghost_state` struct: id, pos, vel, color (rgb), name, anim_state, grapple_state
- [ ] Add `ghost_manager` (likely owned by `instance`): `unordered_map<id, ghost_state>`
- [ ] Implement `draw::draw_ghost(...)` with 50% alpha — does NOT enter collision world, NOT in `state.m_inputs`
- [ ] Implement JS-callable C ABI (all `extern "C"`, prefixed `sr_`):
  - [ ] `sr_set_local_identity(const char* name, float r, float g, float b)`
  - [ ] `sr_load_map(const char* path)`
  - [ ] `sr_push_ghost(const char* id, float pos_x, float pos_y, float vel_x, float vel_y, int8_t facing, uint8_t anim, uint8_t grapple_active, float gx_origin, float gy_origin, float gx_attach, float gy_attach, float g_length, uint8_t g_taut, /* color + name passed once via separate setter */)`
  - [ ] `sr_set_ghost_identity(const char* id, const char* name, float r, float g, float b)`
  - [ ] `sr_remove_ghost(const char* id)`
  - [ ] `sr_get_local_snapshot(uint8_t* out_buf, size_t buf_size) -> size_t` (returns bytes written)
  - [ ] `sr_get_player_screen_pos(const char* id, float* out_x, float* out_y) -> int` (0 = local, returns 1 if id known)
- [ ] Render path: after local player draw, iterate ghost map, draw each at 50% alpha (+ ghost grapple if active)
- [ ] **Exit gate**: hardcoded ghost shows up at fixed position in desktop build, follows fake snapshots

### 4e. Map loading via argument

- [ ] Remove hardcoded `INIT_LOAD_LEVEL` from `playground.h:4`
- [ ] `playground` constructor (and/or `init`) takes a map path string
- [ ] Verify all 4 maps load + play correctly in desktop build
- [ ] **Exit gate**: desktop binary takes map path as CLI arg and loads any of the 4 maps

---

## Phase 5 — Emscripten build

*Depends on Phase 4 complete.*

- [ ] Add `cmake/emscripten.cmake` toolchain config
- [ ] Strip `<GL/glew.h>` for web target; use `<GLES3/gl3.h>` instead
- [ ] Strip `<thread>` / `<mutex>` for web target if unused (single-threaded WASM build)
- [ ] Set Emscripten flags:
  - `-sUSE_GLFW=3 -sFULL_ES3=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2`
  - `-sUSE_ZLIB=1 -sALLOW_MEMORY_GROWTH=1`
  - `-sEXPORTED_FUNCTIONS='["_main","_sr_set_local_identity","_sr_load_map","_sr_push_ghost","_sr_set_ghost_identity","_sr_remove_ghost","_sr_get_local_snapshot","_sr_get_player_screen_pos"]'`
  - `-sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","HEAPF32","stringToUTF8","UTF8ToString"]'`
  - `--preload-file game/assets/maps@/maps`
- [ ] Write `game/platform/web_main.cpp` using `emscripten_set_main_loop_arg`
- [ ] Output `sr.js` + `sr.wasm` + `sr.data` into `apps/web/public/`
- [ ] Add `bun run build:wasm` script
- [ ] **Exit gate**: opening the React app loads the WASM, renders Pitfall, local player is keyboard-controllable

---

## Phase 6 — Wire it together

*Depends on Phases 2, 3, 5.*

- [ ] React `<Game>` component: mount canvas, await Module ready
- [ ] On mount: `sr_set_local_identity(name, r, g, b)` + `sr_load_map(/maps/<id>.sr)`
- [ ] Set up 30Hz interval (`setInterval` or RAF-based clock): `sr_get_local_snapshot()` → WS `snapshot`
- [ ] On WS `snapshot` from peer: `sr_set_ghost_identity` (if first time) + `sr_push_ghost(...)`
- [ ] On WS `player_left`: `sr_remove_ghost(id)`
- [ ] Per-`requestAnimationFrame` name overlay: for each ghost id, `sr_get_player_screen_pos`, render `<div className="player-label">name</div>` with `transform: translate(x, y)`
- [ ] Also overlay local player's own name above their character
- [ ] Disable browser shortcuts that conflict with game keys (focus-locked canvas, preventDefault on space/arrows)
- [ ] **Exit gate**: 2 browser tabs in the same room see each other moving as 50%-opacity colored ghosts with name labels above

---

## Phase 7 — Polish + deploy

- [ ] Production builds for web + server (`bun run build`)
- [ ] Env vars for WS URL (dev vs prod)
- [ ] Error states: server unreachable, room not found, WASM load failure
- [ ] Reconnect UX: show "reconnecting…" instead of dropping the user
- [ ] Deploy frontend (Cloudflare Pages or Vercel) with WASM MIME type configured
- [ ] Deploy server (Fly.io with persistent process)
- [ ] Update README with run + deploy instructions
- [ ] **Exit gate**: shareable URL works end-to-end with a friend on a different network

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

## Non-goals

- Server-authoritative simulation, anti-cheat
- Shared-camera "trailing player dies" mechanic (by design — see AGENTS.md)
- Direct player↔player interaction (grapples don't pull ghosts; items don't hit ghosts)
- Matchmaking, accounts, persistence, leaderboards
- Mobile / touch controls
- Spectator mode, voice chat
