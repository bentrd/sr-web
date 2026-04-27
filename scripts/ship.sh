#!/usr/bin/env bash
# Ship everything live in one command:
#   1. Commit pending changes (if any)
#   2. Push current branch to origin
#   3. Deploy snapshot-relay server to Fly.io
#   4. Build + publish web app to gh-pages
#
# Usage:
#   bash scripts/ship.sh "feat: my commit message"
#   bash scripts/ship.sh                            # only if working tree is clean
#
# Env knobs (skip individual steps when iterating):
#   SKIP_COMMIT=1   don't commit even if dirty (push whatever's already committed)
#   SKIP_PUSH=1     skip git push
#   SKIP_FLY=1      skip Fly server deploy
#   SKIP_PAGES=1    skip gh-pages deploy
#   REMOTE=origin   git remote (forwarded to deploy-pages.sh)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

REMOTE="${REMOTE:-origin}"
COMMIT_MSG="${1:-}"

step() { printf "\n\033[1;36m==> %s\033[0m\n" "$*"; }
warn() { printf "\033[1;33m!! %s\033[0m\n" "$*"; }

# ---------------------------------------------------------------- 1. commit
if [ "${SKIP_COMMIT:-0}" != "1" ]; then
  if [ -n "$(git status --porcelain)" ]; then
    if [ -z "$COMMIT_MSG" ]; then
      warn "Working tree is dirty but no commit message was provided."
      echo "    Pass one as the first argument:"
      echo "      bash scripts/ship.sh \"feat: my change\""
      echo "    Or set SKIP_COMMIT=1 to push the existing commit only."
      exit 1
    fi
    step "Committing pending changes"
    git add -A
    git commit -m "$COMMIT_MSG"
  else
    step "Working tree clean — nothing to commit"
  fi
else
  step "SKIP_COMMIT=1 — skipping commit"
fi

# ---------------------------------------------------------------- 2. push
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
if [ "${SKIP_PUSH:-0}" != "1" ]; then
  step "Pushing $BRANCH to $REMOTE"
  if git rev-parse --abbrev-ref --symbolic-full-name "@{upstream}" >/dev/null 2>&1; then
    git push "$REMOTE" "$BRANCH"
  else
    git push -u "$REMOTE" "$BRANCH"
  fi
else
  step "SKIP_PUSH=1 — skipping push"
fi

# ---------------------------------------------------------------- 3. fly
if [ "${SKIP_FLY:-0}" != "1" ]; then
  step "Building WASM (game + replay validator)"
  bash "$ROOT/scripts/build-wasm.sh"

  step "Deploying server to Fly.io"
  if ! command -v fly >/dev/null 2>&1; then
    warn "fly CLI not found. Install: https://fly.io/docs/hands-on/install-flyctl/"
    exit 1
  fi
  # Build context must be the repo root (Dockerfile reaches into packages/).
  # The replay validator (apps/server/wasm/sr_replay.{js,wasm}) was just
  # built into the Docker context above so it ends up in the image.
  fly deploy \
    --config apps/server/fly.toml \
    --dockerfile apps/server/Dockerfile \
    --remote-only
else
  step "SKIP_FLY=1 — skipping Fly deploy"
fi

# ---------------------------------------------------------------- 4. pages
if [ "${SKIP_PAGES:-0}" != "1" ]; then
  step "Deploying web app to GitHub Pages"
  REMOTE="$REMOTE" bash "$ROOT/scripts/deploy-pages.sh"
else
  step "SKIP_PAGES=1 — skipping gh-pages deploy"
fi

step "All done — live in prod"
echo "    Server: https://sr-web-server.fly.dev"
echo "    Web:    https://bentrd.github.io/sr-web/"
