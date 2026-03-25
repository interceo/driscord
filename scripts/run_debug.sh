#!/usr/bin/env bash
# Debug launcher for the Kotlin/Compose Desktop client.
# Handles JNI segfaults by either enabling core dumps + JVM crash logs (default)
# or wrapping the JVM in GDB for interactive debugging (--gdb flag).
#
# Usage:
#   ./scripts/run_debug.sh          # core dump + JVM crash log mode
#   ./scripts/run_debug.sh --gdb    # interactive GDB mode
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
COMPOSE_DIR="$ROOT/client-compose"
CRASH_DIR="$ROOT/crash_logs"
GDB_MODE=0
DEBUG_BUILD=0

for arg in "$@"; do
    case "$arg" in
        --gdb)         GDB_MODE=1 ;;
        --debug-build) DEBUG_BUILD=1 ;;
    esac
done

find_native_lib() {
    local candidates=(
        "$1/client/libdriscord_jni.so"
        "$1/client/libdriscord_jni.dylib"
    )
    for f in "${candidates[@]}"; do
        if [ -f "$f" ]; then
            dirname "$f"
            return 0
        fi
    done
    return 1
}

# --debug-build: prefer build-debug/, fall back to build/ if not yet compiled
if [ "$DEBUG_BUILD" -eq 1 ]; then
    if ! NATIVE_DIR=$(find_native_lib "$ROOT/build-debug"); then
        echo "==> Debug library not found — run ./scripts/build_client_debug.sh first."
        echo "    Falling back to release build..."
        DEBUG_BUILD=0
    fi
fi

if [ "$DEBUG_BUILD" -eq 0 ] && ! NATIVE_DIR=$(find_native_lib "$BUILD"); then
    echo "==> JNI library not found — building C++ first..."
    bash "$(dirname "$0")/build.sh"
    NATIVE_DIR=$(find_native_lib "$BUILD") || {
        echo "ERROR: libdriscord_jni not found even after build."
        exit 1
    }
fi

mkdir -p "$CRASH_DIR"

# Enable core dumps
ulimit -c unlimited
# Write cores to crash_logs/ instead of cwd
echo "$CRASH_DIR/core.%e.%p" | sudo tee /proc/sys/kernel/core_pattern > /dev/null 2>&1 || \
    echo "WARN: Could not set core_pattern (no sudo). Cores will go to cwd."

# JVM flags: crash log + native memory tracking
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} \
  -XX:ErrorFile=$CRASH_DIR/jvm_crash_%p.log \
  -XX:+CreateCoredumpOnCrash \
  -Xss4m"

export DRISCORD_NATIVE_LIB_DIR="$NATIVE_DIR"

if [ "$GDB_MODE" -eq 1 ]; then
    if ! command -v gdb &>/dev/null; then
        echo "ERROR: gdb not found. Install with: sudo pacman -S gdb"
        exit 1
    fi

    # Create a temporary java wrapper that launches gdb around the real JVM.
    # GDB needs special signal handling because HotSpot uses SIGSEGV internally
    # for null-pointer checks — we want to catch *our* crashes, not those.
    WRAP_DIR="$(mktemp -d)"
    trap 'rm -rf "$WRAP_DIR"' EXIT

    REAL_JAVA="$(command -v java)"
    GDB_INIT="$WRAP_DIR/gdbinit"
    cat > "$GDB_INIT" <<'GDBINIT'
# JVM uses SIGSEGV/SIGBUS for its own purposes — pass them through by default.
handle SIGSEGV nostop noprint pass
handle SIGBUS  nostop noprint pass
# But if execution stops anyway (i.e. a *real* crash), print a full backtrace.
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
    echo "    GDB init       : $GDB_INIT"
    echo "    Crash logs     : $CRASH_DIR"
    echo
    echo "    GDB signals: SIGSEGV/SIGBUS are passed to JVM by default."
    echo "    If we hit a *real* crash, GDB will stop and print a backtrace."
    echo "    Type 'c' to continue after each JVM-internal signal if needed."
    echo

    PATH="$WRAP_DIR:$PATH" exec "$COMPOSE_DIR/gradlew" -p "$COMPOSE_DIR" run
else
    echo "==> Launching Driscord (crash-log mode) ..."
    echo "    Native lib dir : $NATIVE_DIR"
    echo "    Crash logs     : $CRASH_DIR"
    echo "    On crash, inspect:"
    echo "      $CRASH_DIR/jvm_crash_<pid>.log   — JVM hs_err log"
    echo "      $CRASH_DIR/core.<name>.<pid>      — core dump (if ulimit worked)"
    echo "    To load core:  gdb $NATIVE_DIR/libdriscord_jni.so $CRASH_DIR/core.*"
    echo

    exec "$COMPOSE_DIR/gradlew" -p "$COMPOSE_DIR" run
fi
