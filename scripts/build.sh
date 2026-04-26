#!/usr/bin/env bash
# Unified Driscord build script.
#
# TARGET  (mutually exclusive):
#   (none)      Qt6/QML client (default)
#   --qt        Qt6/QML client (explicit)
#   --server    Signaling server
#   --api       Python/FastAPI backend
#   --windows   Windows core tests (MinGW cross-compile, requires --test)
#
# ACTION  (mutually exclusive):
#   (none)      Build artifacts
#   --debug     Build with Debug symbols
#   --test      Build & run tests
#   --bench     Build & run benchmarks
#
# Examples:
#   ./scripts/build.sh                       # Qt client (release)
#   ./scripts/build.sh --debug               # Qt client (debug)
#   ./scripts/build.sh --server              # signaling server (release)
#   ./scripts/build.sh --server --test       # server tests
#   ./scripts/build.sh --api                 # Python API setup
#   ./scripts/build.sh --windows --test      # core tests on Windows under Wine
#   ./scripts/build.sh --test                # core unit/integration tests
#   ./scripts/build.sh --bench               # core benchmarks
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILDS_DIR="$ROOT/.builds"

# ---------------------------------------------------------------------------
# Parse flags
# ---------------------------------------------------------------------------
BUILD_TYPE="Release"
TARGET="qt"       # qt | server | api | windows
ACTION="build"    # build | test | bench

for arg in "$@"; do
    case "$arg" in
        --debug)   BUILD_TYPE="Debug" ;;
        --release) BUILD_TYPE="Release" ;;
        --server)  TARGET="server" ;;
        --api)     TARGET="api" ;;
        --windows) TARGET="windows" ;;
        --qt)      TARGET="qt" ;;
        --test)    ACTION="test" ;;
        --bench)   ACTION="bench" ;;
    esac
done

TYPE_LOWER="${BUILD_TYPE,,}"
JOBS=$(nproc 2>/dev/null || echo 4)

# Linux CMake build dir (used by core test/bench actions and server)
LINUX_BUILD="$BUILDS_DIR/cmake/linux-$TYPE_LOWER"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

cmake_configure() {
    local build_dir="$1"; shift
    if [ ! -f "$build_dir/CMakeCache.txt" ]; then
        echo "==> Configuring CMake ($BUILD_TYPE)..."
        local gen_flag=()
        command -v ninja &>/dev/null && gen_flag=(-G Ninja) || gen_flag=(-G "Unix Makefiles")
        cmake -S "$ROOT" -B "$build_dir" "${gen_flag[@]}" \
            -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -Wno-dev "$@"
    fi
}

# ---------------------------------------------------------------------------
# ===== SERVER =====
# ---------------------------------------------------------------------------
if [ "$TARGET" = "server" ]; then
    if [ "$ACTION" = "test" ]; then
        echo "==> Configuring CMake for server tests ($BUILD_TYPE)..."
        cmake_configure "$LINUX_BUILD" \
            -DBUILD_TESTS=ON -DBUILD_SERVER=ON -DBUILD_CORE=ON
        echo "==> Building server tests ($BUILD_TYPE, $JOBS jobs)..."
        cmake --build "$LINUX_BUILD" --target test_room_isolation -j"$JOBS"
        cd "$LINUX_BUILD"
        ctest -R "test_room_isolation" --output-on-failure
        exit 0
    fi
    if [ "$ACTION" = "bench" ]; then
        echo "No benchmarks for server yet."
        exit 0
    fi
    OUT="$BUILDS_DIR/server/$TYPE_LOWER"
    cmake_configure "$LINUX_BUILD" -DBUILD_CORE=OFF
    echo "==> Building server ($BUILD_TYPE, $JOBS jobs)..."
    cmake --build "$LINUX_BUILD" --target driscord_server -j"$JOBS"
    mkdir -p "$OUT"
    cp "$LINUX_BUILD/backend/signaling_server/driscord_server" "$OUT/"
    echo "==> Server ready: $OUT/driscord_server"
    exit 0
fi

# ---------------------------------------------------------------------------
# ===== API =====
# ---------------------------------------------------------------------------
if [ "$TARGET" = "api" ]; then
    if [ "$ACTION" = "bench" ]; then
        echo "No benchmarks for API yet."
        exit 0
    fi
    API_DIR="$ROOT/backend/api"
    VENV_DIR="$API_DIR/.venv"
    if [ ! -d "$VENV_DIR" ]; then
        echo "==> Creating Python venv..."
        python3 -m venv "$VENV_DIR"
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
    echo "==> API ready. Run with: ./scripts/run.sh --api"
    exit 0
fi

# ---------------------------------------------------------------------------
# ===== WINDOWS (core tests under MinGW + Wine) =====
# ---------------------------------------------------------------------------
if [ "$TARGET" = "windows" ]; then
    if [ "$ACTION" != "test" ]; then
        echo "ERROR: --windows only supports --test (core unit tests under Wine)." >&2
        echo "       The Qt client does not have a Windows packaging target yet." >&2
        exit 1
    fi

    WIN_TEST_DIR="$BUILDS_DIR/cmake/windows-test"
    TOOLCHAIN="$ROOT/cmake/toolchain-mingw64.cmake"
    GEN_FLAGS=()
    command -v ninja &>/dev/null && GEN_FLAGS=(-G Ninja)

    if ! command -v x86_64-w64-mingw32-g++ &>/dev/null && \
       ! command -v x86_64-w64-mingw32-g++-posix &>/dev/null; then
        echo "ERROR: mingw-w64 not found (x86_64-w64-mingw32-g++)." >&2
        echo "       Install: pacman -S mingw-w64-gcc" >&2
        exit 1
    fi

    if [ ! -f "$WIN_TEST_DIR/CMakeCache.txt" ]; then
        echo "==> Configuring CMake (Windows/MinGW, tests)..."
        cmake -S "$ROOT" -B "$WIN_TEST_DIR" "${GEN_FLAGS[@]}" \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SERVER=OFF \
            -DBUILD_TESTS=ON \
            -Wno-dev \
            || { echo "ERROR: CMake Windows test configure failed." >&2; exit 1; }
    fi

    echo "==> Building Windows tests ($JOBS jobs)..."
    cmake --build "$WIN_TEST_DIR" -j"$JOBS" \
        || { echo "ERROR: Windows test build failed." >&2; exit 1; }

    if ! command -v wine64 &>/dev/null && ! command -v wine &>/dev/null; then
        echo "==> Wine not found — test executables built but not executed"
        exit 0
    fi

    GCC_VMAJ=$(x86_64-w64-mingw32-g++-posix -dumpversion 2>/dev/null \
               || x86_64-w64-mingw32-g++ -dumpversion 2>/dev/null \
               || echo "12")
    GCC_VMAJ="${GCC_VMAJ%%.*}"
    # Wine requires Windows-style paths (Z:/) separated by semicolons.
    unix_to_wine() { echo "Z:$(echo "$1" | sed 's|/|\\|g')"; }
    WIN_DLL_PATH="$(unix_to_wine "/usr/lib/gcc/x86_64-w64-mingw32/${GCC_VMAJ}");$(unix_to_wine "/usr/x86_64-w64-mingw32/bin")"
    [ -d "$ROOT/third_party/windows/ffmpeg/bin"  ] && WIN_DLL_PATH="$WIN_DLL_PATH;$(unix_to_wine "$ROOT/third_party/windows/ffmpeg/bin")"
    [ -d "$ROOT/third_party/windows/openssl/bin" ] && WIN_DLL_PATH="$WIN_DLL_PATH;$(unix_to_wine "$ROOT/third_party/windows/openssl/bin")"
    export WINEPATH="${WIN_DLL_PATH}${WINEPATH:+;$WINEPATH}"
    export WINEDEBUG=-all

    echo "==> Initializing Wine prefix..."
    wineboot --init 2>/dev/null || true

    echo "==> Running Windows unit tests under Wine (network integration tests excluded)..."
    cd "$WIN_TEST_DIR"
    ctest --output-on-failure \
        -E "test_datachannel_transport|test_room_isolation|test_net_conditions" \
        --timeout 120
    exit 0
fi

# ---------------------------------------------------------------------------
# ===== QT CLIENT (default) =====
# ---------------------------------------------------------------------------

# --- Test ---
if [ "$ACTION" = "test" ]; then
    TEST_BUILD="$BUILDS_DIR/cmake/linux-test"
    echo "==> Configuring CMake for tests ($BUILD_TYPE)..."
    cmake_configure "$TEST_BUILD" \
        -DBUILD_TESTS=ON -DBUILD_SERVER=ON -DBUILD_CORE=ON
    echo "==> Building tests ($BUILD_TYPE, $JOBS jobs)..."
    cmake --build "$TEST_BUILD" -j"$JOBS"
    cd "$TEST_BUILD"
    ctest --output-on-failure
    exit 0
fi

# --- Bench ---
if [ "$ACTION" = "bench" ]; then
    echo "==> Configuring CMake for benchmarks ($BUILD_TYPE)..."
    cmake_configure "$LINUX_BUILD" \
        -DBUILD_BENCHMARKS=ON -DBUILD_CORE=ON -DBUILD_SERVER=OFF
    echo "==> Building benchmarks ($JOBS jobs)..."
    cmake --build "$LINUX_BUILD" \
        --target bench_jitter bench_protocol bench_video_codec bench_net_conditions \
        -j"$JOBS"
    echo ""
    echo "=== bench_jitter ==="
    DRISCORD_LOG_LEVEL=none "$LINUX_BUILD/core/benchmarks/bench_jitter"
    echo ""
    echo "=== bench_protocol ==="
    DRISCORD_LOG_LEVEL=none "$LINUX_BUILD/core/benchmarks/bench_protocol"
    echo ""
    echo "=== bench_video_codec ==="
    DRISCORD_LOG_LEVEL=none "$LINUX_BUILD/core/benchmarks/bench_video_codec"
    echo ""
    echo "=== bench_net_conditions ==="
    DRISCORD_LOG_LEVEL=none "$LINUX_BUILD/core/benchmarks/bench_net_conditions"
    exit 0
fi

# --- Build (default) ---
QT_BUILD="$BUILDS_DIR/cmake/qt-$TYPE_LOWER"

if [ ! -f "$QT_BUILD/CMakeCache.txt" ]; then
    echo "==> Configuring CMake for Qt client ($BUILD_TYPE)..."
    cmake -S "$ROOT" -B "$QT_BUILD" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DBUILD_QT_CLIENT=ON \
        -DBUILD_SERVER=OFF \
        -Wno-dev
fi

echo "==> Building Qt client ($JOBS jobs)..."
cmake --build "$QT_BUILD" --target driscord_client -j"$JOBS"
echo "==> Qt client ready: $QT_BUILD/client-qt/driscord_client"
