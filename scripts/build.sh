#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"

if [ ! -f "$BUILD/Makefile" ] && [ ! -f "$BUILD/build.ninja" ]; then
    echo "==> Configuring CMake..."
    if command -v ninja &>/dev/null; then
        cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -Wno-dev
    else
        cmake -S "$ROOT" -B "$BUILD" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -Wno-dev
    fi
fi

JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
echo "==> Building with $JOBS jobs..."
cmake --build "$BUILD" -j"$JOBS"

echo "==> Done"
echo "    Server: $BUILD/server/driscord_server"
echo "    Client: $BUILD/client/driscord_client"
