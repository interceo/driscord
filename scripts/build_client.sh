#!/usr/bin/env bash
# Build the Driscord Kotlin/Compose + Kotlin/Native client.
#
# Produces: builds/client-compose/
#   driscord.jar              — uber-JAR (JVM, platform-independent)
#   libdriscord_jni.so        — Linux JNI native library (for JVM client)
#   libdriscord_capi.so       — Linux C-API native library (for Kotlin/Native client)
#   driscord                  — Linux native binary (Kotlin/Native, linuxX64)
#   driscord_jni.dll          — Windows JNI native library (if --windows passed)
#   driscord.json             — runtime config
#   driscord.sh               — Linux JVM launcher
#   driscord.bat              — Windows JVM launcher
#
# Usage:
#   ./scripts/build_client.sh            # Linux only
#   ./scripts/build_client.sh --windows  # Linux + Windows (requires mingw-w64)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
BUILD_WIN="$ROOT/build-win"
COMPOSE_DIR="$ROOT/client-compose"
OUT="$ROOT/builds/client-compose"
BUILDS_DIR="$ROOT/builds"

BUILD_WINDOWS=false
for arg in "$@"; do
    [ "$arg" = "--windows" ] && BUILD_WINDOWS=true
done

# ---------------------------------------------------------------------------
# 1. Linux native libraries (JNI + CAPI)
# ---------------------------------------------------------------------------
if [ ! -f "$BUILD/build.ninja" ] && [ ! -f "$BUILD/Makefile" ]; then
    echo "==> Configuring CMake (Linux)..."
    # Explicitly pin to gcc/ar using absolute paths to prevent CMake from
    # resolving tool names relative to the source directory (e.g. picking up
    # a stray llvm-lib or treating "ar" as a relative path).
    GCC_PATH="$(command -v gcc)"
    GXX_PATH="$(command -v g++)"
    AR_PATH="$(command -v ar)"
    RANLIB_PATH="$(command -v ranlib)"
    CMAKE_EXTRA_FLAGS=(
        -DCMAKE_C_COMPILER="$GCC_PATH"
        -DCMAKE_CXX_COMPILER="$GXX_PATH"
        -DCMAKE_AR="$AR_PATH"
        -DCMAKE_RANLIB="$RANLIB_PATH"
    )
    if command -v ninja &>/dev/null; then
        cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -Wno-dev \
            "${CMAKE_EXTRA_FLAGS[@]}"
    else
        cmake -S "$ROOT" -B "$BUILD" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -Wno-dev \
            "${CMAKE_EXTRA_FLAGS[@]}"
    fi
fi

JOBS=$(nproc 2>/dev/null || echo 4)
echo "==> Building JNI library (Linux, $JOBS jobs)..."
cmake --build "$BUILD" --target driscord_jni -j"$JOBS" 2>/dev/null || \
    cmake --build "$BUILD" --target driscord_core -j"$JOBS"

echo "==> Building C-API library (Linux, $JOBS jobs)..."
cmake --build "$BUILD" --target driscord_capi -j"$JOBS"

# ---------------------------------------------------------------------------
# 2. Windows cross-compiled JNI DLL (optional)
# ---------------------------------------------------------------------------
if $BUILD_WINDOWS; then
    TOOLCHAIN="$ROOT/cmake/toolchain-mingw64.cmake"
    MINGW_SYSROOT="/usr/x86_64-w64-mingw32"
    if ! command -v x86_64-w64-mingw32-g++ &>/dev/null && \
       ! command -v x86_64-w64-mingw32-g++-posix &>/dev/null; then
        echo "WARNING: mingw-w64 not found. Skipping Windows DLL build."
        echo "         Install: sudo pacman -S mingw-w64-gcc   # Arch"
        echo "                  sudo apt install mingw-w64      # Debian/Ubuntu"
    elif [ ! -f "$MINGW_SYSROOT/lib/libssl.a" ] && \
         [ ! -f "$MINGW_SYSROOT/lib/libssl.dll.a" ]; then
        echo "WARNING: mingw-w64 OpenSSL not found at $MINGW_SYSROOT/lib/libssl.a"
        echo "         Install: sudo pacman -S mingw-w64-openssl   # Arch"
        echo "         (Ubuntu: build openssl from source with MinGW toolchain)"
        echo "         Skipping Windows DLL build."
    else
        echo "==> Configuring CMake (Windows/MinGW)..."
        # Wipe stale cache so USE_MBEDTLS/USE_OPENSSL flags take effect cleanly.
        rm -rf "$BUILD_WIN"

        # Find JDK for JNI headers (include/win32 ships with any JDK on Linux)
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
            cmake --build "$BUILD_WIN" --target driscord_jni -j"$JOBS" 2>/dev/null || \
                cmake --build "$BUILD_WIN" --target driscord_core -j"$JOBS" || {
                echo "WARNING: Windows JNI build failed. Skipping."
            }
        fi
    fi
fi

# ---------------------------------------------------------------------------
# 3. Kotlin/Compose fatJar  +  Kotlin/Native linuxX64 binary
# ---------------------------------------------------------------------------
echo ""
echo "==> Building Kotlin clients..."

if [ ! -f "$COMPOSE_DIR/gradlew" ]; then
    echo "    gradlew not found — bootstrapping..."
    if ! command -v gradle &>/dev/null; then
        echo "ERROR: Gradle not installed. Run: cd $COMPOSE_DIR && gradle wrapper"
        exit 1
    fi
    (cd "$COMPOSE_DIR" && gradle wrapper --quiet)
    chmod +x "$COMPOSE_DIR/gradlew"
fi

export DRISCORD_NATIVE_LIB_DIR="$BUILD/client"
export GRADLE_USER_HOME="$BUILDS_DIR/gradle-home"

GRADLE_PROPS=(-PbuildsDir="$BUILDS_DIR" -PclientBuildDir="$OUT")

echo "    [3a] fatJar (JVM)..."
(cd "$COMPOSE_DIR" && ./gradlew fatJar --quiet "${GRADLE_PROPS[@]}")

echo "    [3b] Kotlin/Native binary..."
OS="$(uname -s)"
ARCH="$(uname -m)"
if [ "$OS" = "Linux" ]; then
    NATIVE_TASK="linkDriscordReleaseExecutableLinuxX64"
elif [ "$OS" = "Darwin" ] && [ "$ARCH" = "arm64" ]; then
    NATIVE_TASK="linkDriscordReleaseExecutableMacosArm64"
elif [ "$OS" = "Darwin" ]; then
    NATIVE_TASK="linkDriscordReleaseExecutableMacosX64"
else
    NATIVE_TASK=""
fi

if [ -n "$NATIVE_TASK" ]; then
    (cd "$COMPOSE_DIR" && ./gradlew "$NATIVE_TASK" --quiet "${GRADLE_PROPS[@]}") || \
        echo "    WARNING: Kotlin/Native build failed — skipping native binary."
else
    echo "    WARNING: Unknown OS ($OS/$ARCH), skipping native binary."
fi

# ---------------------------------------------------------------------------
# 4. Stage everything → builds/client-compose/
# ---------------------------------------------------------------------------
mkdir -p "$OUT"

# Native Linux libraries
if ls "$BUILD/client/libdriscord_jni."* &>/dev/null 2>&1; then
    cp "$BUILD/client/libdriscord_jni."* "$OUT/"
fi
if ls "$BUILD/client/libdriscord_capi."* &>/dev/null 2>&1; then
    cp "$BUILD/client/libdriscord_capi."* "$OUT/"
fi

# Kotlin/Native binary (platform-specific)
for BIN_DIR in \
    "$BUILDS_DIR/kotlin/bin/linuxX64/driscordReleaseExecutable" \
    "$BUILDS_DIR/kotlin/bin/macosX64/driscordReleaseExecutable" \
    "$BUILDS_DIR/kotlin/bin/macosArm64/driscordReleaseExecutable"
do
    for EXT in .kexe ""; do
        NATIVE_EXE="$BIN_DIR/driscord$EXT"
        if [ -f "$NATIVE_EXE" ]; then
            cp "$NATIVE_EXE" "$OUT/driscord-native"
            chmod +x "$OUT/driscord-native"
            break 2
        fi
    done
done

# Native Windows DLL (if built)
if $BUILD_WINDOWS && ls "$BUILD_WIN/client/driscord_jni."* &>/dev/null 2>&1; then
    cp "$BUILD_WIN/client/driscord_jni."* "$OUT/"
elif $BUILD_WINDOWS && ls "$BUILD_WIN/client/libdriscord_jni."* &>/dev/null 2>&1; then
    cp "$BUILD_WIN/client/libdriscord_jni."* "$OUT/"
fi

# Runtime config
cp "$ROOT/driscord.json" "$OUT/"

# Linux launcher
cat > "$OUT/driscord.sh" << 'LAUNCHER'
#!/usr/bin/env sh
DIR="$(cd "$(dirname "$0")" && pwd)"
exec java -Djava.library.path="$DIR" -jar "$DIR/driscord.jar" "$@"
LAUNCHER
chmod +x "$OUT/driscord.sh"

# Windows launcher
cat > "$OUT/driscord.bat" << 'LAUNCHER'
@echo off
setlocal
set "DIR=%~dp0"
java -Djava.library.path="%DIR%" -jar "%DIR%driscord.jar" %*
LAUNCHER

echo ""
echo "==> Client build ready: $OUT"
ls -lh "$OUT" 2>/dev/null || true
