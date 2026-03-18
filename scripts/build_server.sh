#!/usr/bin/env bash
# Build the Driscord signaling server (C++ / Boost.Beast).
# Output: builds/server/driscord_server
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
OUT="$ROOT/builds/server"

# ---------------------------------------------------------------------------
# CMake configure (shared build dir with the client build)
# ---------------------------------------------------------------------------
if [ ! -f "$BUILD/build.ninja" ] && [ ! -f "$BUILD/Makefile" ]; then
    echo "==> Configuring CMake..."
    if command -v ninja &>/dev/null; then
        cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -Wno-dev
    else
        cmake -S "$ROOT" -B "$BUILD" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -Wno-dev
    fi
fi

# ---------------------------------------------------------------------------
# Build only the server target
# ---------------------------------------------------------------------------
JOBS=$(nproc 2>/dev/null || echo 4)
echo "==> Building server ($JOBS jobs)..."
cmake --build "$BUILD" --target driscord_server -j"$JOBS"

# ---------------------------------------------------------------------------
# Stage artifacts → builds/server/
# ---------------------------------------------------------------------------
mkdir -p "$OUT"
cp "$BUILD/server/driscord_server" "$OUT/"

echo ""
echo "==> Server ready: $OUT/driscord_server"
