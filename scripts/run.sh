#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

BUILD_TYPE="release"
TARGET="qt"
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

if [ "$TARGET" = "server" ]; then
    PRESET="server"
    [ "$BUILD_TYPE" = "debug" ] && PRESET="server-debug"
    SERVER_BIN="$ROOT/.builds/${DRISCORD_BUILD_TAG:-}$PRESET/backend/signaling_server/driscord_server"
    if [ ! -f "$SERVER_BIN" ]; then
        echo "==> Server binary not found — building..."
        cmake --workflow --preset "$PRESET"
    fi
    if [ -z "${DRISCORD_API_URL:-}" ] \
        && [ "${DRISCORD_ALLOW_ANONYMOUS:-}" != "1" ]; then
        export DRISCORD_API_URL="http://127.0.0.1:${DRISCORD_API_PORT:-8000}"
        echo "==> DRISCORD_API_URL not set, using $DRISCORD_API_URL"
    fi
    echo "==> Launching server ($BUILD_TYPE)..."
    exec "$SERVER_BIN"
fi

if [ "$TARGET" = "api" ]; then
    API_DIR="$ROOT/backend/api"
    VENV_DIR="$API_DIR/.venv"
    if [ ! -d "$VENV_DIR" ]; then
        echo "==> Venv not found — creating it..."
        python3 -m venv "$VENV_DIR"
        "$VENV_DIR/bin/pip" install -q -r "$API_DIR/requirements.txt"
    fi
    echo "==> Launching API server..."
    cd "$API_DIR"
    exec "$VENV_DIR/bin/python" main.py
fi

PRESET="client"
[ "$BUILD_TYPE" = "debug" ] && PRESET="client-debug"
QT_BIN="$ROOT/.builds/${DRISCORD_BUILD_TAG:-}$PRESET/client-qt/driscord_client"
if [ ! -f "$QT_BIN" ]; then
    echo "==> Qt client binary not found — building..."
    cmake --workflow --preset "$PRESET"
fi

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

    echo "==> Launching Driscord (Qt) under GDB..."
    echo "    Binary     : $QT_BIN"
    echo "    Crash logs : $CRASH_DIR"
    exec gdb -ex run --args "$QT_BIN"
fi

echo "==> Launching Qt client ($BUILD_TYPE)..."
exec "$QT_BIN"
