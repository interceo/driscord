#!/usr/bin/env bash
# Unified Driscord build script.
#
# Usage:
#   ./scripts/build.sh                    # build client (release)
#   ./scripts/build.sh --debug            # build client (debug)
#   ./scripts/build.sh --server           # build server (release)
#   ./scripts/build.sh --server --debug   # build server (debug)
#   ./scripts/build.sh --api              # build/setup API
#   ./scripts/build.sh --windows          # cross-compile Windows client (release)
#   ./scripts/build.sh --test             # build & run core tests
#   ./scripts/build.sh --bench            # build & run core benchmarks
#   ./scripts/build.sh --server --test    # (placeholder) test server
#   ./scripts/build.sh --api --test       # (placeholder) test API
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
COMPOSE_DIR="$ROOT/client-compose"

# --- Parse flags ---
BUILD_TYPE="Release"
TARGET="client"      # client | server | api
ACTION="build"       # build | test | bench
BUILD_WINDOWS=false

for arg in "$@"; do
    case "$arg" in
        --debug)   BUILD_TYPE="Debug" ;;
        --release) BUILD_TYPE="Release" ;;
        --server)  TARGET="server" ;;
        --api)     TARGET="api" ;;
        --test)    ACTION="test" ;;
        --bench)   ACTION="bench" ;;
        --windows) BUILD_WINDOWS=true ;;
    esac
done

TYPE_LOWER="${BUILD_TYPE,,}"
BUILD="$ROOT/.builds/cmake/linux-$TYPE_LOWER"
JOBS=$(nproc 2>/dev/null || echo 4)

# --- Helper: cmake configure ---
cmake_configure() {
    local build_dir="$1"; shift
    if [ ! -f "$build_dir/CMakeCache.txt" ]; then
        echo "==> Configuring CMake ($BUILD_TYPE)..."
        if command -v ninja &>/dev/null; then
            cmake -S "$ROOT" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -Wno-dev "$@"
        else
            cmake -S "$ROOT" -B "$build_dir" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -Wno-dev "$@"
        fi
    fi
}

# ===== SERVER =====
if [ "$TARGET" = "server" ]; then
    if [ "$ACTION" = "test" ]; then
        echo "No tests for server yet."
        exit 0
    fi
    if [ "$ACTION" = "bench" ]; then
        echo "No benchmarks for server yet."
        exit 0
    fi
    OUT="$ROOT/.builds/server/$TYPE_LOWER"
    cmake_configure "$BUILD" -DBUILD_CORE=OFF
    echo "==> Building server ($BUILD_TYPE, $JOBS jobs)..."
    cmake --build "$BUILD" --target driscord_server -j"$JOBS"
    mkdir -p "$OUT"
    cp "$BUILD/backend/signaling_server/driscord_server" "$OUT/"
    echo "==> Server ready: $OUT/driscord_server"
    exit 0
fi

# ===== API =====
if [ "$TARGET" = "api" ]; then
    if [ "$ACTION" = "test" ]; then
        echo "No tests for API yet."
        exit 0
    fi
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
    echo "==> Installing API dependencies..."
    "$VENV_DIR/bin/pip" install -q -r "$API_DIR/requirements.txt"
    echo "==> API ready. Run with: ./scripts/run.sh --api"
    exit 0
fi

# ===== CLIENT =====

# --- Test ---
if [ "$ACTION" = "test" ]; then
    # test_datachannel_transport links driscord_core and driscord_signaling,
    # so BUILD_CORE and BUILD_SERVER must be ON.  Always pass flags so switching
    # from --bench to --test (same build dir) re-enables BUILD_TESTS.
    echo "==> Configuring CMake for tests ($BUILD_TYPE)..."
    if command -v ninja &>/dev/null; then
        cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
            -DBUILD_TESTS=ON -DBUILD_SERVER=ON -DBUILD_CORE=ON -Wno-dev
    else
        cmake -S "$ROOT" -B "$BUILD" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
            -DBUILD_TESTS=ON -DBUILD_SERVER=ON -DBUILD_CORE=ON -Wno-dev
    fi
    echo "==> Building tests ($BUILD_TYPE, $JOBS jobs)..."
    cmake --build "$BUILD" -j"$JOBS"
    cd "$BUILD"
    ctest --output-on-failure
    exit 0
fi

# --- Bench ---
if [ "$ACTION" = "bench" ]; then
    # Always pass flags explicitly — cmake_configure skips if cache exists,
    # but build mode can change (e.g. --test → --bench on the same build dir).
    echo "==> Configuring CMake for benchmarks ($BUILD_TYPE)..."
    if command -v ninja &>/dev/null; then
        cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
            -DBUILD_BENCHMARKS=ON -DBUILD_CORE=ON -DBUILD_SERVER=OFF -Wno-dev
    else
        cmake -S "$ROOT" -B "$BUILD" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
            -DBUILD_BENCHMARKS=ON -DBUILD_CORE=ON -DBUILD_SERVER=OFF -Wno-dev
    fi
    echo "==> Building benchmarks ($JOBS jobs)..."
    cmake --build "$BUILD" --target bench_jitter bench_protocol bench_video_codec -j"$JOBS"
    echo ""
    echo "=== bench_jitter ==="
    DRISCORD_LOG_LEVEL=none "$BUILD/core/benchmarks/bench_jitter"
    echo ""
    echo "=== bench_protocol ==="
    DRISCORD_LOG_LEVEL=none "$BUILD/core/benchmarks/bench_protocol"
    echo ""
    echo "=== bench_video_codec ==="
    DRISCORD_LOG_LEVEL=none "$BUILD/core/benchmarks/bench_video_codec"
    exit 0
fi

# --- Build (default) ---
OUT="$ROOT/.builds/client/linux/$TYPE_LOWER"
BUILDS_DIR="$ROOT/.builds"

# 1. Linux native JNI library
cmake_configure "$BUILD"
echo "==> Building JNI library (Linux $BUILD_TYPE, $JOBS jobs)..."
cmake --build "$BUILD" --target driscord_core_jni -j"$JOBS" 2>/dev/null || \
    cmake --build "$BUILD" --target driscord_core -j"$JOBS"

# 2. Windows cross-compilation (optional)
if $BUILD_WINDOWS; then
    BUILD_WIN="$ROOT/.builds/cmake/windows-release"
    OUT_WIN="$ROOT/.builds/client/windows/release"
    TOOLCHAIN="$ROOT/cmake/toolchain-mingw64.cmake"
    MINGW_SYSROOT="/usr/x86_64-w64-mingw32"

    if ! command -v x86_64-w64-mingw32-g++ &>/dev/null && \
       ! command -v x86_64-w64-mingw32-g++-posix &>/dev/null; then
        echo "WARNING: mingw-w64 not found. Skipping Windows DLL build."
    elif [ ! -f "$MINGW_SYSROOT/lib/libssl.a" ] && \
         [ ! -f "$MINGW_SYSROOT/lib/libssl.dll.a" ]; then
        echo "WARNING: mingw-w64 OpenSSL not found. Skipping Windows DLL build."
    else
        echo "==> Configuring CMake (Windows/MinGW)..."
        rm -rf "$BUILD_WIN"

        JDK_HOME="${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(which java)")")")}"
        WIN_JNI_INCLUDE=""
        if [ -d "$JDK_HOME/include/win32" ]; then
            WIN_JNI_INCLUDE="-DJNI_INCLUDE_DIRS_EXTRA=$JDK_HOME/include;$JDK_HOME/include/win32"
        fi

        cmake -S "$ROOT" -B "$BUILD_WIN" \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SERVER=OFF \
            -Wno-dev \
            $WIN_JNI_INCLUDE || {
            echo "WARNING: CMake Windows configure failed. Skipping Windows DLL."
            BUILD_WINDOWS=false
        }

        if $BUILD_WINDOWS; then
            echo "==> Building JNI DLL (Windows, $JOBS jobs)..."
            cmake --build "$BUILD_WIN" --target driscord_core_jni -j"$JOBS" 2>/dev/null || \
                cmake --build "$BUILD_WIN" --target driscord_core -j"$JOBS" || {
                echo "WARNING: Windows JNI build failed. Skipping."
            }
        fi
    fi
fi

# 3. Kotlin/Compose fatJar
echo ""
echo "==> Building Kotlin/Compose client (fatJar)..."

if [ ! -f "$COMPOSE_DIR/gradlew" ]; then
    echo "    gradlew not found — bootstrapping..."
    if ! command -v gradle &>/dev/null; then
        echo "ERROR: Gradle not installed. Run: cd $COMPOSE_DIR && gradle wrapper"
        exit 1
    fi
    (cd "$COMPOSE_DIR" && gradle wrapper --quiet)
    chmod +x "$COMPOSE_DIR/gradlew"
fi

export DRISCORD_NATIVE_LIB_DIR="$BUILD/core"
export GRADLE_USER_HOME="$BUILDS_DIR/gradle-home"

(cd "$COMPOSE_DIR" && ./gradlew fatJar --quiet \
    -PbuildsDir="$BUILDS_DIR" \
    -PclientBuildDir="$OUT")

# 4. Stage artifacts
mkdir -p "$OUT"

if ls "$BUILD/core/libcore."* &>/dev/null 2>&1; then
    cp "$BUILD/core/libcore."* "$OUT/"
fi

cp "$ROOT/driscord.json" "$OUT/"

cat > "$OUT/driscord.sh" << 'LAUNCHER'
#!/usr/bin/env sh
DIR="$(cd "$(dirname "$0")" && pwd)"
exec java -Djava.library.path="$DIR" -jar "$DIR/driscord.jar" "$@"
LAUNCHER
chmod +x "$OUT/driscord.sh"

# Stage Windows artifacts
if $BUILD_WINDOWS; then
    mkdir -p "$OUT_WIN"
    if ls "$BUILD_WIN/core/core."* &>/dev/null 2>&1; then
        cp "$BUILD_WIN/core/core."* "$OUT_WIN/"
    elif ls "$BUILD_WIN/core/libcore."* &>/dev/null 2>&1; then
        cp "$BUILD_WIN/core/libcore."* "$OUT_WIN/"
    fi
    [ -f "$OUT/driscord.jar" ] && cp "$OUT/driscord.jar" "$OUT_WIN/"
    cp "$ROOT/driscord.json" "$OUT_WIN/"
    cat > "$OUT_WIN/driscord.bat" << 'LAUNCHER'
@echo off
setlocal
set "DIR=%~dp0"
java -Djava.library.path="%DIR%" -jar "%DIR%driscord.jar" %*
LAUNCHER
fi

echo ""
echo "==> Client build ready: $OUT"
ls -lh "$OUT"
