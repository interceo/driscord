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
    export DRISCORD_NATIVE_LIB_DIR="$BUILD/client"

    BUILDS_DIR="$ROOT/builds"
    export GRADLE_USER_HOME="$BUILDS_DIR/gradle-home"

    # fatJar — один uber-JAR без bundled JRE
    (cd "$COMPOSE_DIR" && ./gradlew fatJar --quiet -PbuildsDir="$BUILDS_DIR")

    # Пакуем: jar + нативные .so + лаунчер
    DIST="$BUILDS_DIR/dist"
    STAGING="$DIST/compose-staging"
    rm -rf "$STAGING" && mkdir -p "$STAGING"

    cp "$DIST/driscord.jar" "$STAGING/"
    [ -f "$BUILD/client/libdriscord_jni.so" ] && cp "$BUILD/client/libdriscord_jni.so" "$STAGING/"
    cp "$BUILD/client"/lib*.so* "$STAGING/" 2>/dev/null || true
    cp "$ROOT/driscord.json" "$STAGING/"

    # Лаунчер
    cat > "$STAGING/driscord.sh" << 'EOF'
#!/usr/bin/env sh
DIR="$(cd "$(dirname "$0")" && pwd)"
exec java -Djava.library.path="$DIR" -jar "$DIR/driscord.jar" "$@"
EOF
    chmod +x "$STAGING/driscord.sh"

    (cd "$STAGING" && zip -qr "$DIST/driscord_compose.zip" .)
    rm -rf "$STAGING"
    echo "    Compose zip: $DIST/driscord_compose.zip"
fi

echo ""
echo "==> Done"
