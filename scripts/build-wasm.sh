#!/usr/bin/env bash
# Build the WASM game bundle (sr.js + sr.wasm + sr.data) into apps/web/public/.
#
# Why this script exists: brew's emscripten on macOS picks up the system
# python (which is too old) and configures binaryen against /usr/local
# (which doesn't have wasm-opt). We override both so the build is
# reproducible without manual one-time setup.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v emcmake >/dev/null 2>&1; then
  echo "error: emcmake not found. Install with: brew install emscripten" >&2
  exit 1
fi

# brew emscripten ships its own python+llvm+binaryen but doesn't set them
# in the auto-generated config. Pin them here to the brew install.
export EMSDK_PYTHON="${EMSDK_PYTHON:-/opt/homebrew/bin/python3}"

BUILD_DIR="$ROOT/game/build-web"
BUILD_TYPE="${BUILD_TYPE:-Release}"

echo "==> Configuring (Emscripten, $BUILD_TYPE)"
emcmake cmake \
  -S "$ROOT/game" \
  -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  >/dev/null

echo "==> Building"
cmake --build "$BUILD_DIR" -j

echo
echo "==> Output:"
ls -la "$ROOT/apps/web/public/sr."{js,wasm,data}
