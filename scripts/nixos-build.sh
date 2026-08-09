#!/usr/bin/env bash
# NixOS build entry point. Mirrors scripts/build.sh but keeps outputs isolated.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILDS_DIR="$ROOT/.builds/nixos"

if [ "${DRISCORD_NIXOS_ENV:-}" != "1" ]; then
    command -v nix >/dev/null || {
        echo "ERROR: nix is required for scripts/nixos-build.sh." >&2
        exit 1
    }
    exec nix develop "$ROOT" -c bash "$0" "$@"
fi

BUILD_TYPE="Release"
TARGET="qt"
ACTION="build"
for arg in "$@"; do
    case "$arg" in
        --debug) BUILD_TYPE="Debug" ;;
        --release) BUILD_TYPE="Release" ;;
        --qt) TARGET="qt" ;;
        --server) TARGET="server" ;;
        --api) TARGET="api" ;;
        --test) ACTION="test" ;;
        --bench)
            echo "ERROR: Legacy media benchmarks were removed." >&2
            exit 1
            ;;
        --windows)
            echo "ERROR: Google WebRTC core artifacts are currently Linux-only." >&2
            exit 1
            ;;
        *)
            echo "ERROR: Unknown argument: $arg" >&2
            exit 1
            ;;
    esac
done

TYPE_LOWER="${BUILD_TYPE,,}"
JOBS="$(nproc 2>/dev/null || echo 4)"

configure() {
    local build_dir="$1"
    shift
    if [ ! -f "$build_dir/CMakeCache.txt" ]; then
        cmake -S "$ROOT" -B "$build_dir" -G Ninja \
            -DCMAKE_C_COMPILER=clang \
            -DCMAKE_CXX_COMPILER=clang++ \
            -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -Wno-dev "$@"
    fi
}

if [ "$TARGET" = "api" ]; then
    API_DIR="$ROOT/backend/api"
    VENV_DIR="$API_DIR/.venv"
    [ -d "$VENV_DIR" ] || python -m venv "$VENV_DIR"
    if [ "$ACTION" = "test" ]; then
        "$VENV_DIR/bin/pip" install -q -r "$API_DIR/requirements-dev.txt"
        cd "$API_DIR"
        exec "$VENV_DIR/bin/pytest"
    fi
    "$VENV_DIR/bin/pip" install -q -r "$API_DIR/requirements.txt"
    exit 0
fi

if [ "$TARGET" = "server" ] && [ "$ACTION" = "build" ]; then
    BUILD_DIR="$BUILDS_DIR/cmake/server-$TYPE_LOWER"
    OUT="$BUILDS_DIR/server/$TYPE_LOWER"
    configure "$BUILD_DIR" -DBUILD_CORE=OFF -DBUILD_SERVER=ON \
        -DBUILD_TESTS=OFF -DBUILD_QT_CLIENT=OFF
    cmake --build "$BUILD_DIR" --target driscord_server -j"$JOBS"
    mkdir -p "$OUT"
    cp "$BUILD_DIR/backend/signaling_server/driscord_server" "$OUT/"
    exit 0
fi

if [ "$ACTION" = "test" ]; then
    BUILD_DIR="$BUILDS_DIR/cmake/google-webrtc-test-$TYPE_LOWER"
    configure "$BUILD_DIR" -DBUILD_CORE=ON -DBUILD_SERVER=ON \
        -DBUILD_TESTS=ON -DBUILD_QT_CLIENT=OFF
    if [ "$TARGET" = "server" ]; then
        cmake --build "$BUILD_DIR" --target test_room_isolation -j"$JOBS"
        ctest --test-dir "$BUILD_DIR" -R '^test_room_isolation$' \
            --output-on-failure
    else
        cmake --build "$BUILD_DIR" -j"$JOBS"
        ctest --test-dir "$BUILD_DIR" --output-on-failure
    fi
    exit 0
fi

BUILD_DIR="$BUILDS_DIR/cmake/qt-webrtc-$TYPE_LOWER"
configure "$BUILD_DIR" -DBUILD_CORE=ON -DBUILD_SERVER=OFF \
    -DBUILD_TESTS=OFF -DBUILD_QT_CLIENT=ON
cmake --build "$BUILD_DIR" --target driscord_client -j"$JOBS"
echo "==> Qt client ready: $BUILD_DIR/client-qt/driscord_client"
