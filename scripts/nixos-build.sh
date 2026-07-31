#!/usr/bin/env bash
# NixOS-only Driscord build script.
#
# Usage mirrors scripts/build.sh, but all native dependencies come from the
# repository Nix dev shell and build outputs live under .builds/nixos/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILDS_DIR="$ROOT/.builds/nixos"

enter_nix_develop() {
    if [ "${DRISCORD_NIXOS_ENV:-}" = "1" ]; then
        return
    fi

    if ! command -v nix >/dev/null 2>&1; then
        echo "ERROR: nix is required for scripts/nixos-build.sh." >&2
        exit 1
    fi

    exec nix develop "$ROOT" -c bash "$0" "$@"
}

enter_nix_develop "$@"

BUILD_TYPE="Release"
TARGET="qt"
ACTION="build"

for arg in "$@"; do
    case "$arg" in
        --debug)   BUILD_TYPE="Debug" ;;
        --release) BUILD_TYPE="Release" ;;
        --server)  TARGET="server" ;;
        --api)     TARGET="api" ;;
        --qt)      TARGET="qt" ;;
        --test)    ACTION="test" ;;
        --bench)   ACTION="bench" ;;
        --windows)
            echo "ERROR: Windows cross-tests are intentionally not part of the NixOS build script." >&2
            exit 1
            ;;
        *)
            echo "ERROR: Unknown argument: $arg" >&2
            exit 1
            ;;
    esac
done

TYPE_LOWER="${BUILD_TYPE,,}"
JOBS=$(nproc 2>/dev/null || echo 4)
NIX_CMAKE_COMMON=(-G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -Wno-dev)

cmake_configure() {
    local build_dir="$1"; shift
    if [ ! -f "$build_dir/CMakeCache.txt" ]; then
        echo "==> Configuring CMake for NixOS ($BUILD_TYPE): $build_dir"
        cmake -S "$ROOT" -B "$build_dir" "${NIX_CMAKE_COMMON[@]}" "$@"
    fi
}

if [ "$TARGET" = "api" ]; then
    if [ "$ACTION" = "bench" ]; then
        echo "No benchmarks for API yet."
        exit 0
    fi

    API_DIR="$ROOT/backend/api"
    VENV_DIR="$API_DIR/.venv"
    if [ ! -d "$VENV_DIR" ]; then
        echo "==> Creating Python venv..."
        python -m venv "$VENV_DIR"
    fi

    if [ "$ACTION" = "test" ]; then
        echo "==> Installing API dev dependencies..."
        "$VENV_DIR/bin/pip" install -q -r "$API_DIR/requirements-dev.txt"
        echo "==> Running pytest..."
        cd "$API_DIR"
        exec "$VENV_DIR/bin/pytest"
    fi

    echo "==> Installing API dependencies..."
    "$VENV_DIR/bin/pip" install -q -r "$API_DIR/requirements.txt"
    echo "==> API ready. Run with: ./scripts/nixos-run.sh --api"
    exit 0
fi

if [ "$ACTION" = "test" ]; then
    TEST_BUILD="$BUILDS_DIR/cmake/test-$TYPE_LOWER"
    cmake_configure "$TEST_BUILD" \
        -DBUILD_TESTS=ON -DBUILD_SERVER=ON -DBUILD_CORE=ON

    if [ "$TARGET" = "server" ]; then
        echo "==> Building server tests ($BUILD_TYPE, $JOBS jobs)..."
        cmake --build "$TEST_BUILD" --target test_room_isolation -j"$JOBS"
        cd "$TEST_BUILD"
        ctest -R "test_room_isolation" --output-on-failure
        exit 0
    fi

    echo "==> Building tests ($BUILD_TYPE, $JOBS jobs)..."
    cmake --build "$TEST_BUILD" -j"$JOBS"
    cd "$TEST_BUILD"
    ctest --output-on-failure
    exit 0
fi

if [ "$ACTION" = "bench" ]; then
    if [ "$TARGET" = "server" ]; then
        echo "No benchmarks for server yet."
        exit 0
    fi

    BENCH_BUILD="$BUILDS_DIR/cmake/bench-$TYPE_LOWER"
    cmake_configure "$BENCH_BUILD" \
        -DBUILD_BENCHMARKS=ON -DBUILD_CORE=ON -DBUILD_SERVER=OFF
    echo "==> Building benchmarks ($JOBS jobs)..."
    cmake --build "$BENCH_BUILD" \
        --target bench_playout bench_protocol bench_video_codec bench_net_conditions \
        -j"$JOBS"

    echo ""
    echo "=== bench_playout ==="
    DRISCORD_LOG_LEVEL=none "$BENCH_BUILD/core/benchmarks/bench_playout"
    echo ""
    echo "=== bench_protocol ==="
    DRISCORD_LOG_LEVEL=none "$BENCH_BUILD/core/benchmarks/bench_protocol"
    echo ""
    echo "=== bench_video_codec ==="
    DRISCORD_LOG_LEVEL=none "$BENCH_BUILD/core/benchmarks/bench_video_codec"
    echo ""
    echo "=== bench_net_conditions ==="
    DRISCORD_LOG_LEVEL=none "$BENCH_BUILD/core/benchmarks/bench_net_conditions"
    exit 0
fi

if [ "$TARGET" = "server" ]; then
    SERVER_BUILD="$BUILDS_DIR/cmake/server-$TYPE_LOWER"
    SERVER_OUT="$BUILDS_DIR/server/$TYPE_LOWER"
    cmake_configure "$SERVER_BUILD" \
        -DBUILD_CORE=OFF -DBUILD_SERVER=ON -DBUILD_QT_CLIENT=OFF
    echo "==> Building signaling server ($BUILD_TYPE, $JOBS jobs)..."
    cmake --build "$SERVER_BUILD" --target driscord_server -j"$JOBS"
    mkdir -p "$SERVER_OUT"
    cp "$SERVER_BUILD/backend/signaling_server/driscord_server" "$SERVER_OUT/"
    echo "==> Server ready: $SERVER_OUT/driscord_server"
    exit 0
fi

QT_BUILD="$BUILDS_DIR/cmake/qt-$TYPE_LOWER"
cmake_configure "$QT_BUILD" \
    -DBUILD_QT_CLIENT=ON -DBUILD_SERVER=OFF -DBUILD_CORE=ON
echo "==> Building Qt client ($BUILD_TYPE, $JOBS jobs)..."
cmake --build "$QT_BUILD" --target driscord_client -j"$JOBS"
echo "==> Qt client ready: $QT_BUILD/client-qt/driscord_client"
