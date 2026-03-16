#!/usr/bin/env bash
# Launch the Kotlin/Compose Desktop client.
# Builds the native JNI library first if it is not found.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
COMPOSE_DIR="$ROOT/client-compose"

# Locate the shared library (Linux .so / macOS .dylib)
find_native_lib() {
    local candidates=(
        "$BUILD/client/libdriscord_jni.so"
        "$BUILD/client/libdriscord_jni.dylib"
    )
    for f in "${candidates[@]}"; do
        if [ -f "$f" ]; then
            dirname "$f"
            return 0
        fi
    done
    return 1
}

if ! NATIVE_DIR=$(find_native_lib); then
    echo "==> JNI library not found — building C++ first..."
    bash "$(dirname "$0")/build.sh"
    NATIVE_DIR=$(find_native_lib) || {
        echo "ERROR: libdriscord_jni not found even after build."
        echo "  Make sure JNI headers are available (install openjdk-dev / jdk-devel)."
        exit 1
    }
fi

if [ ! -f "$COMPOSE_DIR/gradlew" ]; then
    echo "==> gradlew not found — bootstrapping Gradle wrapper..."
    if ! command -v gradle &>/dev/null; then
        echo "ERROR: Gradle is not installed. Run 'cd $COMPOSE_DIR && gradle wrapper' manually."
        exit 1
    fi
    (cd "$COMPOSE_DIR" && gradle wrapper --quiet)
    chmod +x "$COMPOSE_DIR/gradlew"
fi

echo "==> Launching Driscord (Compose) ..."
echo "    Native lib dir: $NATIVE_DIR"
export DRISCORD_NATIVE_LIB_DIR="$NATIVE_DIR"
exec "$COMPOSE_DIR/gradlew" -p "$COMPOSE_DIR" run
