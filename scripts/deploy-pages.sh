#!/usr/bin/env bash
# Build the web app + WASM bundle and push the result to the `gh-pages`
# branch so GitHub Pages serves it.
#
# Why a script (not a GH Action): the WASM build needs Emscripten + the
# Steam Workshop .sr maps, neither of which lives in CI. Build locally,
# publish the dist.
#
# Env knobs:
#   VITE_WS_URL  — WebSocket endpoint baked into the bundle.
#                  Defaults to the Fly.io app name we use in fly.toml.
#   VITE_BASE    — Public base path. Defaults to /sr-web/ (the repo name).
#   REMOTE       — git remote to push to. Defaults to `origin`.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VITE_WS_URL="${VITE_WS_URL:-wss://sr-web-server.fly.dev/ws}"
VITE_BASE="${VITE_BASE:-/sr-web/}"
REMOTE="${REMOTE:-origin}"
DIST="$ROOT/apps/web/dist"

echo "==> Collecting maps from Steam Workshop"
bun run collect-maps

echo "==> Collecting trails from Steam Workshop"
bun run collect-trails

echo "==> Building WASM (Emscripten)"
bash "$ROOT/scripts/build-wasm.sh"

echo "==> Building web app"
echo "    VITE_WS_URL=$VITE_WS_URL"
echo "    VITE_BASE=$VITE_BASE"
( cd apps/web && VITE_WS_URL="$VITE_WS_URL" VITE_BASE="$VITE_BASE" bun run build )

# GitHub Pages serves any /<route> as a 404 unless we provide a fallback
# that loads index.html. With HashRouter we don't strictly need this, but
# a 404.html that mirrors index.html keeps deep links cheap.
cp "$DIST/index.html" "$DIST/404.html"
# `.nojekyll` stops GitHub from filtering out underscore-prefixed paths.
touch "$DIST/.nojekyll"

echo "==> Publishing to gh-pages branch"

WORKTREE_DIR="$(mktemp -d)/gh-pages"
mkdir -p "$WORKTREE_DIR"

cleanup() {
  git worktree remove --force "$WORKTREE_DIR" >/dev/null 2>&1 || true
  rm -rf "$(dirname "$WORKTREE_DIR")"
}
trap cleanup EXIT

# Create the branch if it doesn't exist locally yet.
if ! git show-ref --verify --quiet refs/heads/gh-pages; then
  git worktree add --orphan -b gh-pages "$WORKTREE_DIR"
else
  git worktree add "$WORKTREE_DIR" gh-pages
fi

# Wipe the worktree (keep .git) and replace with fresh dist.
( cd "$WORKTREE_DIR" && find . -maxdepth 1 -mindepth 1 ! -name '.git' -exec rm -rf {} + )
cp -R "$DIST"/* "$DIST"/.nojekyll "$WORKTREE_DIR"/

(
  cd "$WORKTREE_DIR"
  git add -A
  if git diff --cached --quiet; then
    echo "==> No changes to publish"
    exit 0
  fi
  git commit -m "deploy: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  git push "$REMOTE" gh-pages
)

echo "==> Done. Pages will publish in ~30s."
echo "    Visit: https://bentrd.github.io${VITE_BASE}"
