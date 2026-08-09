#!/usr/bin/env bash
# NixOS-only Driscord launcher.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

enter_nix_develop() {
    if [ "${DRISCORD_NIXOS_ENV:-}" = "1" ]; then
        return
    fi

    if ! command -v nix >/dev/null 2>&1; then
        echo "ERROR: nix is required for scripts/nixos-run.sh." >&2
        exit 1
    fi

    exec nix develop "$ROOT" -c bash "$0" "$@"
}

enter_nix_develop "$@"

BUILD_TYPE="release"
TARGET="qt"
GDB_MODE=0
PASSTHRU=()

for arg in "$@"; do
    case "$arg" in
        --debug)   BUILD_TYPE="debug" ;;
        --release) BUILD_TYPE="release" ;;
        --server)  TARGET="server" ;;
        --api)     TARGET="api" ;;
        --qt)      TARGET="qt" ;;
        --gdb)     GDB_MODE=1 ;;
        *)         PASSTHRU+=("$arg") ;;
    esac
done

if [ "$TARGET" = "server" ]; then
    SERVER_BIN="$ROOT/.builds/nixos/server/$BUILD_TYPE/driscord_server"
    if [ ! -f "$SERVER_BIN" ]; then
        echo "==> Server binary not found; building..."
        TYPE_FLAG="--release"
        [ "$BUILD_TYPE" = "debug" ] && TYPE_FLAG="--debug"
        bash "$ROOT/scripts/nixos-build.sh" --server "$TYPE_FLAG"
    fi
    echo "==> Launching signaling server ($BUILD_TYPE)..."
    exec "$SERVER_BIN" "${PASSTHRU[@]}"
fi

if [ "$TARGET" = "api" ]; then
    API_DIR="$ROOT/backend/api"
    VENV_DIR="$API_DIR/.venv"
    if [ ! -d "$VENV_DIR" ]; then
        echo "==> API venv not found; preparing API..."
        bash "$ROOT/scripts/nixos-build.sh" --api
    fi
    echo "==> Launching API server..."
    cd "$API_DIR"
    exec "$VENV_DIR/bin/python" main.py "${PASSTHRU[@]}"
fi

QT_BIN="$ROOT/.builds/nixos/cmake/qt-webrtc-$BUILD_TYPE/client-qt/driscord_client"
if [ ! -f "$QT_BIN" ]; then
    echo "==> Qt client binary not found; building..."
    TYPE_FLAG="--release"
    [ "$BUILD_TYPE" = "debug" ] && TYPE_FLAG="--debug"
    bash "$ROOT/scripts/nixos-build.sh" --qt "$TYPE_FLAG"
fi

if [ "$GDB_MODE" -eq 1 ]; then
    if ! command -v gdb >/dev/null 2>&1; then
        echo "ERROR: gdb is missing from the Nix dev shell." >&2
        exit 1
    fi
    echo "==> Launching Driscord Qt client under GDB..."
    exec gdb -ex run --args "$QT_BIN" "${PASSTHRU[@]}"
fi

echo "==> Launching Qt client ($BUILD_TYPE)..."
exec "$QT_BIN" "${PASSTHRU[@]}"
