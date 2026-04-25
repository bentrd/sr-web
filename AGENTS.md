# Agent Onboarding — sr-web

**Read this entire file before touching any code.** It captures the non-obvious context that has already cost time on this project.

---

## What we're building

A browser port of [SR-cpp](https://github.com/rbit-sr/SR-cpp) (a from-scratch C++ reimplementation of **SpeedRunners**, originally a 4-player local/online racing platformer) with **ghost-style multiplayer**:

- Each client runs its own complete simulation locally
- Each client streams its own player state at 30Hz over WebSockets
- Remote players are rendered as **50% opacity colored rectangles** with name labels above them
- No collision or interaction between players (visual ghosts only)
- No shared camera, no "trailing player dies" mechanic — by design

Active task list lives in [PLAN.md](./PLAN.md).

---

## Critical gotchas

### ⏱️ Time units: `delta = 33333` is **300Hz**, not 30Hz

`emulation/timespan.h` defines `TICKS_PER_SEC = 10000000` (100ns per tick). This is the **.NET `TimeSpan` convention**, kept for binary compatibility with XNA `.xnb` level files (SpeedRunners is XNA/C#).

So `delta = 33333` ticks = **3.333ms = 300Hz**. The simulation already runs at 300Hz.

If you bump or read tick values, always go through `timespan::seconds()` / `timespan::seconds_f()` — never assume microseconds.

### 🎨 Legacy OpenGL — must rewrite for any web work

The original `drawing/draw_util.cpp` uses **immediate mode** OpenGL (`glBegin`/`glEnd`/`glColor3f`/`glVertex2f`). **WebGL has zero support for this.** There is no shim, no flag, no workaround.

The whole file (~290 lines) gets rewritten to modern GL (VBOs + shaders + `glDrawArrays`) in **PLAN Phase 4b**. The rewrite applies to **both desktop and web** — modern GL has worked everywhere since ~2010, so it's a one-time tax, not a fork.

### 🗺️ Map format: `.sr`, not `.xnb`

Workshop maps are `.sr` files (gzipped custom binary, parsed by `level::level(const char*)` via `decode_gzip`). The `swiftpeaks.xnb` reference at `playground.h:4` (`INIT_LOAD_LEVEL`) is **dead code** — `.xnb` is the original game's format, but the C++ port reads `.sr` and never reads `.xnb`. Remove the hardcode in Phase 4e.

### 👻 Ghosts are render-only — never enter the sim

Remote players (ghosts) are **visual entities only**. They:

- Are NOT added to `collision_engine`
- Are NOT entries in `state.m_inputs`
- Cannot be grappled, knocked, hit by items, or affect the local player's physics
- Are stored in a separate `ghost_manager` (Phase 4d)

This keeps every client's local sim deterministic and means we never need server reconciliation. Don't be tempted to "just put them in collision so grapples can attach" — that breaks the whole architecture.

### 🌐 All networking goes through JS

Single WebSocket per client, opened from the browser JS layer. C++ never opens a socket and never uses `emscripten_websocket_*`. Snapshots cross the boundary via `cwrap`'d C functions:

- Outgoing: JS calls `sr_get_local_snapshot()` every 33ms, sends bytes over WS
- Incoming: WS handler calls `sr_push_ghost(...)` with the parsed payload

This gives us one transport for both lobby messages and game snapshots, and avoids learning Emscripten's WS API.

### 🔢 State already supports 4 players

`emulation/state.h` declares `std::array<std::array<bool, input_count>, 4> m_inputs{}`. The sim is already 4-player-shaped. Don't refactor this away — slots 1–3 are unused in ghost mode but stay reserved for a possible future authoritative-MP mode.

### 🏷️ Name labels are HTML overlays, not GL text

Text rendering inside GL means font atlases, glyph caches, kerning — none of which we're going to maintain. Instead:

- WASM exposes per-frame screen coordinates via `sr_get_player_screen_pos(id, &x, &y)`
- React renders absolutely-positioned `<div>` elements over the canvas
- Easy CSS styling, accessible, internationalizable, free

### 🧊 Three independent clocks — keep them separate

| Clock | Rate | Drives |
|---|---|---|
| Sim tick | 300Hz fixed | Physics, collision, gameplay |
| Render | Monitor refresh (variable, interpolated) | Drawing |
| Network send | 30Hz | `sr_get_local_snapshot` → WS |

Glenn Fiedler "Fix Your Timestep" pattern. Already in place via the accumulator. Do not collapse them.

### 🐛 Existing bug: `draw_util.cpp:25-26`

```cpp
glVertex2f(bounds.max_x, bounds.max_x);  // should be max_y
```
Two of the six vertices in `draw_rectangle(aabb)` use `max_x` for the y-coordinate. Fix it as part of the modern-GL rewrite (Phase 4b).

---

## Stack

| Layer | Choice |
|---|---|
| Game core | C++ (SR-cpp), patched in place |
| Game build | CMake (alongside the existing `.sln`) |
| Web compile | Emscripten → WASM |
| Browser GL | WebGL 2 (`-sFULL_ES3=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2`) |
| Window/input | GLFW3 (native + Emscripten shim `-sUSE_GLFW=3`) |
| Compression | zlib (native + `-sUSE_ZLIB=1`) |
| Lobby UI | Vite + React + TypeScript |
| Server | Bun + native WebSocket |
| Protocol | Shared TS package, manually mirrored to C structs |
| Monorepo | Bun workspaces |
| Hosting | Frontend: Cloudflare Pages / Vercel · Server: Fly.io |

---

## Repo layout

```
sr-web/
├── apps/
│   ├── web/                    # Vite + React lobby + WASM host
│   └── server/                 # Bun WS relay
├── packages/
│   └── protocol/               # Shared TS message types (source of truth)
├── game/
│   ├── upstream/               # SR-cpp submodule
│   ├── CMakeLists.txt          # New build (NOT a replacement for the .sln)
│   ├── platform/
│   │   ├── desktop_main.cpp
│   │   └── web_main.cpp        # emscripten_set_main_loop_arg + sr_* exports
│   └── assets/maps/            # .sr files (gitignored), manifest.json (committed)
├── scripts/
│   ├── collect-maps.ts
│   └── build-wasm.sh
├── PLAN.md                     # Task checklist — claim and tick as you go
├── AGENTS.md                   # This file
├── CLAUDE.md                   # Pointer
└── README.md
```

---

## Conventions

### C ABI for WASM exports

All JS-callable functions must be:
- Declared `extern "C"`
- Prefixed `sr_`
- Take only **primitives + raw pointers** as args (no `std::string`, no STL types across the boundary)
- Return primitives only; use out-pointers for multiple returns

Example:
```cpp
extern "C" int sr_get_player_screen_pos(const char* id, float* out_x, float* out_y);
```

### Snapshot protocol is a contract

The byte layout consumed by `sr_push_ghost` and produced by `sr_get_local_snapshot` is defined in `packages/protocol/src/index.ts`. **When you change a snapshot field, update both sides in the same commit** and bump a `PROTOCOL_VERSION` constant.

For v1, the layout is hand-mirrored. If drift becomes painful, switch to FlatBuffers — but not before.

### Bun, not Node

The repo uses Bun workspaces and Bun runtime. Don't add Node-specific packages without verifying Bun compatibility. Use `Bun.serve` for the WS server, not `ws` or `uWebSockets.js`.

### React: never `dangerouslySetInnerHTML`

Player names appear in HTML overlays. Render through React's auto-escaping. The username field is a free-form string — assume hostile input.

---

## Don'ts

- ❌ Don't commit `.sr` map files (Steam Workshop user content; redistribution unclear)
- ❌ Don't add ghosts to `collision_engine` or `state.m_inputs`
- ❌ Don't open a WebSocket from C++ (`emscripten_websocket_*` is off-limits)
- ❌ Don't render text in GL (HTML overlay is the answer)
- ❌ Don't replace the existing `.sln` — the CMake build coexists
- ❌ Don't merge the three clocks (sim / render / send)
- ❌ Don't assume `delta` is in microseconds (see top of file)
- ❌ Don't pass STL types across the WASM boundary

---

## Useful commands

*Once the corresponding phases are complete, these will exist:*

```bash
bun install                 # Install JS deps
bun dev                     # Run web + server in dev mode
bun run collect-maps        # Pull 4 target maps from local Steam install
bun run build:wasm          # Build the WASM game artifacts
bun run build               # Production build for everything
```

Native desktop build (verify C++ changes don't break it):
```bash
cmake -S game -B game/build-desktop
cmake --build game/build-desktop
./game/build-desktop/sr_desktop game/assets/maps/pitfall.sr
```

WASM build:
```bash
emcmake cmake -S game -B game/build-web -DCMAKE_BUILD_TYPE=Release
cmake --build game/build-web
# outputs sr.{js,wasm,data} into apps/web/public/
```

---

## Reference: the 4 target maps

| ID | Display name | Source filename in Subscribed |
|---|---|---|
| `pitfall` | Pitfall | `76561198172969226.3630381689.Pitfall.sr` |
| `genetics` | Genetics | `76561198147801759.3408575314.Genetics.sr` |
| `grapple_circuit` | Grapple Circuit | `76561198147163751.1399530740.Grapple Circuit.sr` |
| `oasis_abyss` | Oasis Abyss | `76561198172969226.635403453.Oasis - Abyss.sr` |

`scripts/collect-maps.ts` (Phase 1) handles the discovery and copying.
