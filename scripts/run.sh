#!/usr/bin/env bash
# Unified Driscord launcher.
#
# Usage:
#   ./scripts/run.sh                      # run Qt client (release)
#   ./scripts/run.sh --debug              # run Qt client (debug build)
#   ./scripts/run.sh --qt                 # run Qt client (explicit)
#   ./scripts/run.sh --qt --debug         # run Qt client (debug)
#   ./scripts/run.sh --server             # run server (release)
#   ./scripts/run.sh --server --debug     # run server (debug)
#   ./scripts/run.sh --api                # run API server
#   ./scripts/run.sh --gdb                # run Qt client under GDB
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# --- Parse flags ---
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
QT_BIN="$ROOT/.builds/cmake/qt-$BUILD_TYPE/client-qt/driscord_client"
if [ ! -f "$QT_BIN" ]; then
    echo "==> Qt client binary not found — building..."
    TYPE_FLAG="--release"
    [ "$BUILD_TYPE" = "debug" ] && TYPE_FLAG="--debug"
    bash "$(dirname "$0")/build.sh" --qt $TYPE_FLAG
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
