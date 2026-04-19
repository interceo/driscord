#!/usr/bin/env bash
# Unified Driscord build script.
#
# TARGET  (mutually exclusive):
#   (none)      Linux client — native lib + fat JAR
#   --server    Signaling server
#   --api       Python/FastAPI backend
#   --windows   Windows client (MinGW cross-compile)
#
# ACTION  (mutually exclusive):
#   (none)      Build artifacts
#   --debug     Build with Debug symbols (Linux/server only)
#   --test      Build & run tests
#   --bench     Build & run benchmarks
#   --package   Build compact single-file distribution
#               Linux → single AppImage
#               Windows → portable zip with bundled JRE
#
# Examples:
#   ./scripts/build.sh                       # Linux client (release)
#   ./scripts/build.sh --debug               # Linux client (debug)
#   ./scripts/build.sh --package             # Linux AppImage
#   ./scripts/build.sh --server              # signaling server (release)
#   ./scripts/build.sh --server --test       # server tests
#   ./scripts/build.sh --api                 # Python API setup
#   ./scripts/build.sh --windows             # Windows dev build (DLL + JAR)
#   ./scripts/build.sh --windows --package   # Windows portable zip
#   ./scripts/build.sh --test                # core unit/integration tests
#   ./scripts/build.sh --bench               # core benchmarks
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
COMPOSE_DIR="$ROOT/client-compose"
BUILDS_DIR="$ROOT/.builds"

# ---------------------------------------------------------------------------
# Parse flags
# ---------------------------------------------------------------------------
BUILD_TYPE="Release"
TARGET="client"   # client | server | api | windows
ACTION="build"    # build | test | bench | package

for arg in "$@"; do
    case "$arg" in
        --debug)   BUILD_TYPE="Debug" ;;
        --release) BUILD_TYPE="Release" ;;
        --server)  TARGET="server" ;;
        --api)     TARGET="api" ;;
        --windows) TARGET="windows" ;;
        --test)    ACTION="test" ;;
        --bench)   ACTION="bench" ;;
        --package) ACTION="package" ;;
    esac
done

TYPE_LOWER="${BUILD_TYPE,,}"
JOBS=$(nproc 2>/dev/null || echo 4)

# Linux CMake build dir (used by client and test/bench actions)
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

ensure_gradlew() {
    if [ ! -f "$COMPOSE_DIR/gradlew" ]; then
        echo "    gradlew not found — bootstrapping..."
        if ! command -v gradle &>/dev/null; then
            echo "ERROR: Gradle not installed. Run: cd $COMPOSE_DIR && gradle wrapper"
            exit 1
        fi
        (cd "$COMPOSE_DIR" && gradle wrapper --quiet)
        chmod +x "$COMPOSE_DIR/gradlew"
    fi
}

# Builds the Windows JNI DLL via MinGW.
# Sets WIN_BUILD_DIR to the cmake output directory; all progress goes to stderr.
build_windows_dll() {
    WIN_BUILD_DIR="$BUILDS_DIR/cmake/windows-release"
    local toolchain="$ROOT/cmake/toolchain-mingw64.cmake"

    if ! command -v x86_64-w64-mingw32-g++ &>/dev/null && \
       ! command -v x86_64-w64-mingw32-g++-posix &>/dev/null; then
        echo "ERROR: mingw-w64 not found (x86_64-w64-mingw32-g++)." >&2
        echo "       Install: pacman -S mingw-w64-gcc" >&2
        return 1
    fi

    # JAVA_HOME is forwarded through `cmake` env so CMakeLists.txt can locate
    # the host JDK's jni.h when cross-compiling (see core/CMakeLists.txt).
    local jdk_home="${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(which java)")")")}"

    if [ ! -f "$WIN_BUILD_DIR/CMakeCache.txt" ]; then
        echo "==> Configuring CMake (Windows/MinGW)..."
        JAVA_HOME="$jdk_home" cmake -S "$ROOT" -B "$WIN_BUILD_DIR" \
            -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SERVER=OFF \
            -DBUILD_LAUNCHER=ON \
            -Wno-dev \
            || { echo "ERROR: CMake Windows configure failed." >&2; return 1; }
    else
        # Update cache in case of pre-existing build without BUILD_LAUNCHER
        cmake "$WIN_BUILD_DIR" -DBUILD_LAUNCHER=ON -Wno-dev \
            || { echo "ERROR: CMake Windows reconfigure failed." >&2; return 1; }
    fi

    echo "==> Building JNI DLL (Windows, $JOBS jobs)..."
    cmake --build "$WIN_BUILD_DIR" --target driscord_core_jni -j"$JOBS" 2>/dev/null \
        || cmake --build "$WIN_BUILD_DIR" --target driscord_core -j"$JOBS" \
        || { echo "ERROR: Windows JNI build failed." >&2; return 1; }

    echo "==> Building launcher EXE (Windows)..."
    cmake --build "$WIN_BUILD_DIR" --target driscord_launcher -j"$JOBS" \
        || { echo "ERROR: Launcher build failed." >&2; return 1; }
}

# Downloads appimagetool to BUILDS_DIR/tools/ if not already on PATH.
ensure_appimagetool() {
    if ! command -v appimagetool &>/dev/null; then
        local tools_dir="$BUILDS_DIR/tools"
        local tool="$tools_dir/appimagetool"
        mkdir -p "$tools_dir"
        if [ ! -f "$tool" ]; then
            echo "==> Downloading appimagetool..."
            curl -fsSL \
                -o "$tool" \
                "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
            chmod +x "$tool"
        fi
        export PATH="$tools_dir:$PATH"
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
# ===== WINDOWS =====
# ---------------------------------------------------------------------------
if [ "$TARGET" = "windows" ]; then
    if [ "$ACTION" = "bench" ]; then
        echo "Benchmarks are not available for the Windows cross-compile target."
        exit 0
    fi

    if [ "$ACTION" = "test" ]; then
        WIN_TEST_DIR="$BUILDS_DIR/cmake/windows-test"
        TOOLCHAIN="$ROOT/cmake/toolchain-mingw64.cmake"
        GEN_FLAGS=()
        command -v ninja &>/dev/null && GEN_FLAGS=(-G Ninja)

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

    OUT_WIN="$BUILDS_DIR/client/windows/release"
    mkdir -p "$OUT_WIN"

    WIN_BUILD_DIR=""
    build_windows_dll || { echo "ERROR: Windows cross-compilation failed."; exit 1; }
    WIN_DLL=""
    for candidate in \
            "$WIN_BUILD_DIR/core/core.dll" \
            "$WIN_BUILD_DIR/core/libcore.dll"; do
        [ -f "$candidate" ] && WIN_DLL="$candidate" && break
    done
    WIN_LAUNCHER=""
    for candidate in \
            "$WIN_BUILD_DIR/launcher/driscord.exe" \
            "$WIN_BUILD_DIR/launcher/driscord_launcher.exe"; do
        [ -f "$candidate" ] && WIN_LAUNCHER="$candidate" && break
    done

    ensure_gradlew

    # Common: build fat JAR (with embedded native lib if DLL was found)
    export GRADLE_USER_HOME="$BUILDS_DIR/gradle-home"
    if [ -n "$WIN_DLL" ]; then
        NATIVE_STAGING="$BUILDS_DIR/native-staging-win"
        rm -rf "$NATIVE_STAGING" && mkdir -p "$NATIVE_STAGING"
        cp "$WIN_DLL" "$NATIVE_STAGING/core.dll"
        # FFmpeg runtime DLLs — core.dll loads them at startup. Pattern matches
        # BtbN's naming (avcodec-61.dll etc.) and any companion DLLs BtbN ships.
        FFMPEG_BIN="$ROOT/third_party/windows/ffmpeg/bin"
        if [ -d "$FFMPEG_BIN" ]; then
            for dll in "$FFMPEG_BIN"/*.dll; do
                # Skip ffmpeg/ffplay/ffprobe executables (FFmpeg ships .exe, but
                # shell glob is *.dll so we only hit libs anyway).
                [ -f "$dll" ] && cp "$dll" "$NATIVE_STAGING/"
            done
        fi
        export DRISCORD_NATIVE_LIB_DIR="$NATIVE_STAGING"
    else
        unset DRISCORD_NATIVE_LIB_DIR || true
    fi
    unset DRISCORD_EXTRA_NATIVE_DIR || true

    echo ""
    echo "==> Building fat JAR (with embedded Windows DLL)..."
    (cd "$COMPOSE_DIR" && ./gradlew fatJar \
        -PbuildsDir="$BUILDS_DIR" \
        -PclientBuildDir="$OUT_WIN" \
        --quiet)

    if [ "$ACTION" = "package" ]; then
        # ----------------------------------------------------------------
        # Windows portable zip — driscord.exe + JAR + bundled JRE
        # ----------------------------------------------------------------
        WIN_JRE_CACHE="$BUILDS_DIR/windows-jre-cache"
        WIN_JRE_ARCHIVE="$WIN_JRE_CACHE/jre21-win.zip"
        if [ ! -f "$WIN_JRE_ARCHIVE" ]; then
            echo "==> Downloading Windows JRE 21 (Temurin)..."
            mkdir -p "$WIN_JRE_CACHE"
            curl -fsSL \
                -o "$WIN_JRE_ARCHIVE" \
                "https://api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jre/hotspot/normal/eclipse?project=jdk"
        fi

        WIN_JRE_DIR="$OUT_WIN/jre"
        rm -rf "$WIN_JRE_DIR" && mkdir -p "$WIN_JRE_DIR"
        unzip -q "$WIN_JRE_ARCHIVE" -d "$WIN_JRE_DIR"
        JRE_INNER=$(ls "$WIN_JRE_DIR" | head -1)
        if [ -n "$JRE_INNER" ] && [ -d "$WIN_JRE_DIR/$JRE_INNER" ]; then
            mv "$WIN_JRE_DIR/$JRE_INNER"/* "$WIN_JRE_DIR/"
            rmdir "$WIN_JRE_DIR/$JRE_INNER"
        fi

        [ -n "$WIN_LAUNCHER" ] && cp "$WIN_LAUNCHER" "$OUT_WIN/driscord.exe"

        APP_VER=$(grep -oP 'version\s*=\s*"\K[^"]+' "$ROOT/client-compose/build.gradle.kts" | head -1)
        WIN_ZIP="$OUT_WIN/Driscord-${APP_VER}-windows-x64.zip"
        (cd "$OUT_WIN" && zip -r "$WIN_ZIP" driscord.exe driscord.jar jre/)

        echo ""
        echo "==> Windows distribution ready: $OUT_WIN"
        ls -lh "$OUT_WIN"
    else
        # dev build: DLL + FFmpeg runtime DLLs + JAR + launcher
        [ -n "$WIN_DLL" ]      && cp "$WIN_DLL"      "$OUT_WIN/core.dll"
        [ -n "$WIN_LAUNCHER" ] && cp "$WIN_LAUNCHER" "$OUT_WIN/driscord.exe"
        if [ -d "$ROOT/third_party/windows/ffmpeg/bin" ]; then
            for dll in "$ROOT/third_party/windows/ffmpeg/bin"/*.dll; do
                [ -f "$dll" ] && cp "$dll" "$OUT_WIN/"
            done
        fi
        [ -f "$ROOT/config.json" ] && cp "$ROOT/config.json" "$OUT_WIN/" \
            || echo "WARN: config.json not found — skipping (add manually before distributing)"

        echo ""
        echo "==> Windows client ready: $OUT_WIN"
        ls -lh "$OUT_WIN"
    fi
    exit 0
fi

# ---------------------------------------------------------------------------
# ===== CLIENT (Linux) =====
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

# --- Package: Linux AppImage ---
if [ "$ACTION" = "package" ]; then
    OUT_LINUX="$BUILDS_DIR/client/linux/release"
    mkdir -p "$OUT_LINUX"

    # 1. Build native library
    cmake_configure "$LINUX_BUILD"
    echo "==> Building JNI library (Linux Release, $JOBS jobs)..."
    cmake --build "$LINUX_BUILD" --target driscord_core_jni -j"$JOBS" 2>/dev/null \
        || cmake --build "$LINUX_BUILD" --target driscord_core -j"$JOBS"

    # 2. Stage native lib for embedding into JAR
    NATIVE_STAGING="$BUILDS_DIR/native-staging"
    rm -rf "$NATIVE_STAGING" && mkdir -p "$NATIVE_STAGING"
    cp "$LINUX_BUILD/core/libcore.so" "$NATIVE_STAGING/" 2>/dev/null || true

    ensure_gradlew
    ensure_appimagetool

    export DRISCORD_NATIVE_LIB_DIR="$NATIVE_STAGING"
    unset DRISCORD_EXTRA_NATIVE_DIR || true
    export DRISCORD_PACKAGING=1
    export GRADLE_USER_HOME="$BUILDS_DIR/gradle-home"

    echo ""
    echo "==> Building Linux AppImage..."
    (cd "$COMPOSE_DIR" && ./gradlew packageReleaseAppImage \
        -PbuildsDir="$BUILDS_DIR" \
        --quiet)

    APP_VER=$(grep -oP 'version\s*=\s*"\K[^"]+' "$ROOT/client-compose/build.gradle.kts" | head -1)
    APPIMAGE_APPDIR="$BUILDS_DIR/kotlin/compose/binaries/main-release/app/driscord"
    APPIMAGE_DEST="$OUT_LINUX/Driscord-${APP_VER}-x86_64.AppImage"

    if [ ! -d "$APPIMAGE_APPDIR" ]; then
        echo "WARNING: jpackage app-image not found at $APPIMAGE_APPDIR"
    else
        APPDIR_TMP="$BUILDS_DIR/AppDir"
        rm -rf "$APPDIR_TMP"
        cp -r "$APPIMAGE_APPDIR" "$APPDIR_TMP"

        cat > "$APPDIR_TMP/AppRun" << 'APPRUN'
#!/bin/sh
APPDIR="$(dirname "$(readlink -f "$0")")"
exec "$APPDIR/bin/driscord" "$@"
APPRUN
        chmod +x "$APPDIR_TMP/AppRun"

        cat > "$APPDIR_TMP/driscord.desktop" << 'DESKTOP'
[Desktop Entry]
Name=Driscord
Exec=driscord
Icon=driscord
Type=Application
Categories=Network;
Comment=P2P voice and screen sharing
DESKTOP

        if [ -f "$APPDIR_TMP/lib/driscord.png" ]; then
            ln -sf "lib/driscord.png" "$APPDIR_TMP/driscord.png"
        fi

        APPIMAGETOOL_CMD="appimagetool"
        "$APPIMAGETOOL_CMD" --version &>/dev/null 2>&1 \
            || APPIMAGETOOL_CMD="appimagetool --appimage-extract-and-run"

        ARCH=x86_64 $APPIMAGETOOL_CMD "$APPDIR_TMP" "$APPIMAGE_DEST" 2>&1
        chmod +x "$APPIMAGE_DEST"
        echo ""
        echo "==> Linux AppImage ready: $OUT_LINUX"
        ls -lh "$OUT_LINUX"
    fi
    exit 0
fi

# ---------------------------------------------------------------------------
# --- Build (default): dev/CI artifact — libcore.so + fat JAR
# ---------------------------------------------------------------------------
OUT="$BUILDS_DIR/client/linux/$TYPE_LOWER"
mkdir -p "$OUT"

cmake_configure "$LINUX_BUILD"
echo "==> Building JNI library (Linux $BUILD_TYPE, $JOBS jobs)..."
cmake --build "$LINUX_BUILD" --target driscord_core_jni -j"$JOBS" 2>/dev/null \
    || cmake --build "$LINUX_BUILD" --target driscord_core -j"$JOBS"

ensure_gradlew

export DRISCORD_NATIVE_LIB_DIR="$LINUX_BUILD/core"
export GRADLE_USER_HOME="$BUILDS_DIR/gradle-home"

echo ""
echo "==> Building Kotlin/Compose client (fatJar)..."
(cd "$COMPOSE_DIR" && ./gradlew fatJar --quiet \
    -PbuildsDir="$BUILDS_DIR" \
    -PclientBuildDir="$OUT")

ls "$LINUX_BUILD/core/libcore."* &>/dev/null 2>&1 \
    && cp "$LINUX_BUILD/core/libcore."* "$OUT/"

[ -f "$ROOT/config.json" ] && cp "$ROOT/config.json" "$OUT/" \
    || echo "WARN: config.json not found — skipping (add manually before distributing)"

cat > "$OUT/driscord.sh" << 'LAUNCHER'
#!/usr/bin/env sh
DIR="$(cd "$(dirname "$0")" && pwd)"
exec java -Djava.library.path="$DIR" -jar "$DIR/driscord.jar" "$@"
LAUNCHER
chmod +x "$OUT/driscord.sh"

echo ""
echo "==> Client build ready: $OUT"
ls -lh "$OUT"
