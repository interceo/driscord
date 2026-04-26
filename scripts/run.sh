#!/usr/bin/env bash
# Unified Driscord launcher.
#
# Usage:
#   ./scripts/run.sh                      # run client (release)
#   ./scripts/run.sh --debug              # run client (debug build)
#   ./scripts/run.sh --server             # run server (release)
#   ./scripts/run.sh --server --debug     # run server (debug)
#   ./scripts/run.sh --api                # run API server
#   ./scripts/run.sh --qt                 # run Qt client (release)
#   ./scripts/run.sh --qt --debug         # run Qt client (debug build)
#   ./scripts/run.sh --gdb                # run client under GDB
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
COMPOSE_DIR="$ROOT/client-compose"

# --- Parse flags ---
BUILD_TYPE="release"
TARGET="client"
GDB_MODE=0

for arg in "$@"; do
    case "$arg" in
        --debug)   BUILD_TYPE="debug" ;;
        --release) BUILD_TYPE="release" ;;
        --server)  TARGET="server" ;;
        --api)     TARGET="api" ;;
        --qt)      TARGET="qt" ;;
        --gdb)     GDB_MODE=1 ;;
    esac
done

BUILD="$ROOT/.builds/cmake/linux-$BUILD_TYPE"

# ===== SERVER =====
if [ "$TARGET" = "server" ]; then
    SERVER_BIN="$ROOT/.builds/server/$BUILD_TYPE/driscord_server"
    if [ ! -f "$SERVER_BIN" ]; then
        echo "==> Server binary not found — building..."
        TYPE_FLAG="--release"
        [ "$BUILD_TYPE" = "debug" ] && TYPE_FLAG="--debug"
        bash "$(dirname "$0")/build.sh" --server $TYPE_FLAG
    fi
    echo "==> Launching server ($BUILD_TYPE)..."
    exec "$SERVER_BIN" "$@"
fi

# ===== API =====
if [ "$TARGET" = "api" ]; then
    API_DIR="$ROOT/backend/api"
    VENV_DIR="$API_DIR/.venv"
    if [ ! -d "$VENV_DIR" ]; then
        echo "==> Venv not found — building API first..."
        bash "$(dirname "$0")/build.sh" --api
    fi
    echo "==> Launching API server..."
    cd "$API_DIR"
    exec "$VENV_DIR/bin/python" main.py "$@"
fi

# ===== QT CLIENT =====
if [ "$TARGET" = "qt" ]; then
    QT_BIN="$ROOT/.builds/cmake/qt-$BUILD_TYPE/client-qt/driscord_client"
    if [ ! -f "$QT_BIN" ]; then
        echo "==> Qt client binary not found — building..."
        TYPE_FLAG="--release"
        [ "$BUILD_TYPE" = "debug" ] && TYPE_FLAG="--debug"
        bash "$(dirname "$0")/build.sh" --qt $TYPE_FLAG
    fi
    echo "==> Launching Qt client ($BUILD_TYPE)..."
    exec "$QT_BIN"
fi

# ===== CLIENT =====

# --- Release: run the AppImage ---
if [ "$BUILD_TYPE" = "release" ] && [ "$GDB_MODE" -eq 0 ]; then
    APPIMAGE_DIR="$ROOT/.builds/client/linux/release"
    APPIMAGE=$(ls "$APPIMAGE_DIR"/Driscord-*.AppImage 2>/dev/null | head -1)
    if [ -z "$APPIMAGE" ]; then
        echo "ERROR: AppImage not found in $APPIMAGE_DIR"
        echo "  Build it first with: ./scripts/build.sh --package"
        exit 1
    fi
    echo "==> Launching Driscord (AppImage): $APPIMAGE"
    exec "$APPIMAGE"
fi

# --- Debug / GDB: run via Gradle ---
find_native_lib() {
    local candidates=(
        "$BUILD/core/libcore.so"
        "$BUILD/core/libcore.dylib"
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
    echo "==> JNI library not found — building..."
    bash "$(dirname "$0")/build.sh" --debug
    NATIVE_DIR=$(find_native_lib) || {
        echo "ERROR: libcore not found even after build."
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

export DRISCORD_NATIVE_LIB_DIR="$NATIVE_DIR"

if [ "$GDB_MODE" -eq 1 ]; then
    if ! command -v gdb &>/dev/null; then
        echo "ERROR: gdb not found. Install with: sudo pacman -S gdb"
        exit 1
    fi

    CRASH_DIR="$ROOT/crash_logs"
    mkdir -p "$CRASH_DIR"
    ulimit -c unlimited
    echo "$CRASH_DIR/core.%e.%p" | sudo tee /proc/sys/kernel/core_pattern > /dev/null 2>&1 || \
        echo "WARN: Could not set core_pattern (no sudo). Cores will go to cwd."

    export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} \
      -XX:ErrorFile=$CRASH_DIR/jvm_crash_%p.log \
      -XX:+CreateCoredumpOnCrash \
      -Xss4m"

    WRAP_DIR="$(mktemp -d)"
    trap 'rm -rf "$WRAP_DIR"' EXIT

    REAL_JAVA="$(command -v java)"
    GDB_INIT="$WRAP_DIR/gdbinit"
    cat > "$GDB_INIT" <<'GDBINIT'
handle SIGSEGV nostop noprint pass
handle SIGBUS  nostop noprint pass
define hook-stop
  if $_siginfo._sigsigno == 11 || $_siginfo._sigsigno == 7
    echo \n=== SEGFAULT/SIGBUS caught — C++ backtrace: ===\n
    thread apply all bt full
  end
end
run
GDBINIT

    cat > "$WRAP_DIR/java" <<WRAPPER
#!/usr/bin/env bash
exec gdb -x "$GDB_INIT" --args "$REAL_JAVA" "\$@"
WRAPPER
    chmod +x "$WRAP_DIR/java"

    echo "==> Launching Driscord in GDB mode ..."
    echo "    Native lib dir : $NATIVE_DIR"
    echo "    Crash logs     : $CRASH_DIR"
    echo

    PATH="$WRAP_DIR:$PATH" exec "$COMPOSE_DIR/gradlew" -p "$COMPOSE_DIR" run
fi

echo "==> Launching Driscord (debug via Gradle) ..."
echo "    Native lib dir: $NATIVE_DIR"
exec "$COMPOSE_DIR/gradlew" -p "$COMPOSE_DIR" run
