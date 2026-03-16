#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
COMPOSE_DIR="$ROOT/client-compose"

# ---------------------------------------------------------------------------
# 1. C++ — CMake (server + legacy ImGui client + driscord_jni)
# ---------------------------------------------------------------------------
if [ ! -f "$BUILD/Makefile" ] && [ ! -f "$BUILD/build.ninja" ]; then
    echo "==> Configuring CMake..."
    if command -v ninja &>/dev/null; then
        cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -Wno-dev
    else
        cmake -S "$ROOT" -B "$BUILD" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -Wno-dev
    fi
fi

JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
echo "==> Building C++ ($JOBS jobs)..."
cmake --build "$BUILD" -j"$JOBS"

echo ""
echo "    Server:         $BUILD/server/driscord_server"
echo "    Legacy client:  $BUILD/client/driscord_client"
if ls "$BUILD/client/libdriscord_jni."* &>/dev/null 2>&1; then
    echo "    JNI library:    $(ls "$BUILD/client/libdriscord_jni."* | head -1)"
fi

# ---------------------------------------------------------------------------
# 2. Kotlin/Compose Desktop client
# ---------------------------------------------------------------------------
echo ""
echo "==> Building Kotlin/Compose client..."

# Bootstrap Gradle wrapper if it doesn't exist yet
if [ ! -f "$COMPOSE_DIR/gradlew" ]; then
    echo "    gradlew not found — bootstrapping..."
    if ! command -v gradle &>/dev/null; then
        echo "    WARNING: Gradle not found. Install Gradle or generate the wrapper manually:"
        echo "      cd $COMPOSE_DIR && gradle wrapper"
        echo "    Skipping Kotlin build."
    else
        (cd "$COMPOSE_DIR" && gradle wrapper --quiet)
        chmod +x "$COMPOSE_DIR/gradlew"
    fi
fi

if [ -f "$COMPOSE_DIR/gradlew" ]; then
    # Pass native lib dir so the jar knows where to find libdriscord_jni
    export DRISCORD_NATIVE_LIB_DIR="$BUILD/client"
    (cd "$COMPOSE_DIR" && ./gradlew build --quiet)
    echo "    Compose client: $COMPOSE_DIR/build/compose/jars/"
fi

echo ""
echo "==> Done"
