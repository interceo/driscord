#!/usr/bin/env bash
# Build libdriscord_jni.so with debug symbols for GDB backtraces.
# Output: build-debug/client/libdriscord_jni.so
#
# Usage:
#   ./scripts/build_client_debug.sh            # Debug (-O0 -g, best backtraces)
#   ./scripts/build_client_debug.sh --reldeb   # RelWithDebInfo (-O2 -g, faster)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DEBUG="$ROOT/build-debug"

BUILD_TYPE="Debug"
for arg in "$@"; do
    [ "$arg" = "--reldeb" ] && BUILD_TYPE="RelWithDebInfo"
done

JOBS=$(nproc 2>/dev/null || echo 4)

echo "==> Configuring CMake (build type: $BUILD_TYPE)..."
if command -v ninja &>/dev/null; then
    cmake -S "$ROOT" -B "$BUILD_DEBUG" -G Ninja \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DBUILD_SERVER=OFF \
        -Wno-dev
else
    cmake -S "$ROOT" -B "$BUILD_DEBUG" -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DBUILD_SERVER=OFF \
        -Wno-dev
fi

echo "==> Building JNI library ($BUILD_TYPE, $JOBS jobs)..."
cmake --build "$BUILD_DEBUG" --target driscord_jni -j"$JOBS" 2>/dev/null || \
    cmake --build "$BUILD_DEBUG" --target driscord_core -j"$JOBS"

echo ""
echo "==> Debug library ready: $BUILD_DEBUG/client/libdriscord_jni.so"
echo "    Run with:  DRISCORD_NATIVE_LIB_DIR=$BUILD_DEBUG/client ./scripts/run_debug.sh --gdb"
echo "    Or use the convenience wrapper:"
echo "               ./scripts/run_debug.sh --gdb --debug-build"
