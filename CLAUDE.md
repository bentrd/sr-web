# sr-web

Browser port of [SR-cpp](https://github.com/rbit-sr/SR-cpp) (a SpeedRunners reimplementation in C++) with ghost-style multiplayer over WebSockets.

> ## 🛑 Read these before doing anything
>
> 1. **[AGENTS.md](./AGENTS.md)** — non-obvious project context (time units, legacy GL, file formats, ghost rendering rules, etc.). Get this wrong and you will waste hours.
> 2. **[PLAN.md](./PLAN.md)** — the active task checklist. Claim a task by ticking its box in a commit, then implement.

## TL;DR for orientation

- C++ game compiled to WASM with Emscripten
- Vite + React + TypeScript lobby UI hosting the WASM canvas
- Bun + native WebSocket server as a dumb snapshot relay
- Ghost MP: every client runs its own sim; remote players are visual-only at 50% opacity
- Sim runs at **300Hz** (the `delta = 33333` is .NET TimeSpan ticks, not microseconds — see AGENTS.md)
- All networking through JS; C++ never touches a socket

## Common tasks

| You want to | Read |
|---|---|
| Pick up work | [PLAN.md](./PLAN.md) — find an unticked box, claim it |
| Understand a weird convention | [AGENTS.md](./AGENTS.md) — gotchas section |
| Touch C++ rendering | AGENTS.md → "Legacy OpenGL" + PLAN Phase 4b |
| Touch the snapshot format | AGENTS.md → "Snapshot protocol is a contract" |
| Add a JS↔WASM function | AGENTS.md → "C ABI for WASM exports" |

## Don't

- Don't commit `.sr` map files (Steam Workshop content)
- Don't add ghosts to the collision world
- Don't render text inside GL
- See AGENTS.md → "Don'ts" for the full list
