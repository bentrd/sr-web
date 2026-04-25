# sr-web

Browser port of [SR-cpp](https://github.com/rbit-sr/SR-cpp) with ghost-style
multiplayer. Each client runs the full deterministic SpeedRunners simulation
in WASM at 300 Hz; remote players are render-only ghosts at 50% opacity,
broadcast at 30 Hz over WebSockets.

See [AGENTS.md](./AGENTS.md) for the architecture; [PLAN.md](./PLAN.md) for the
phase-by-phase roadmap.

## Layout

```
apps/web        Vite + React lobby + WASM host (the player-facing app)
apps/server     Bun WebSocket relay (no game state, just snapshots + rooms)
packages/protocol  Shared TS types for the WS wire format
game            Vendored SR-cpp + CMake build (desktop + Emscripten)
scripts         Helper scripts (collect-maps, build-wasm)
```

## Prerequisites

- [Bun](https://bun.sh) ≥ 1.0
- [Emscripten](https://emscripten.org) ≥ 4 — `brew install emscripten` on macOS
- CMake ≥ 3.15

## First-time setup

```bash
bun install                  # workspace deps
bun run collect-maps         # copy .sr files from your SpeedRunners install
bun run build:wasm           # build sr.js + sr.wasm + sr.data into apps/web/public
```

`collect-maps` reads from
`~/Library/Application Support/SpeedRunners/CEngineStorage/AllPlayers/Subscribed`
on macOS — adjust `scripts/collect-maps.ts` if your maps live elsewhere.

## Dev

```bash
bun run dev                  # Vite (5173) + Bun WS server (4000) in parallel
```

Open <http://localhost:5173>, pick a name + color, create a room, hit *Start
game*. Share the room code or `/r/CODE` URL with a friend.

## Deploy

### Frontend (Vercel / Cloudflare Pages)

```bash
cd apps/web
VITE_WS_URL=wss://your-server/ws bun run build
# dist/ is the static bundle. Make sure your host serves
# /sr.wasm with Content-Type: application/wasm.
```

### Server (Fly.io)

```bash
fly launch --config apps/server/fly.toml --dockerfile apps/server/Dockerfile
fly deploy --config apps/server/fly.toml --dockerfile apps/server/Dockerfile
```

Fly's free tier handles small player counts. Bigger rooms — pump
`fly scale` accordingly.

## Adding a map

1. Drop the `.sr` into `game/assets/maps/<id>.sr`
2. Add `{ id: "<id>", displayName: "..." }` to `apps/web/src/lobby/maps.ts`
3. `bun run build:wasm` to repackage the preloaded VFS bundle

## Caveats

- 16-bit color picker is full 16M (`<input type="color">`); colors round to
  RGB float triplets in the wire protocol.
- The simulation determinism guarantee only holds for clients on the same
  WASM build — bumping `PROTOCOL_VERSION` invalidates older clients.
- Soft warning at 12 players per room. No hard cap, but bandwidth grows
  O(n²) past that.
