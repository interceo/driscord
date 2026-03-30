#!/usr/bin/env bash
# Launch the Driscord native binary (Kotlin/Native).
# Detects the current platform, finds the binary, and runs it.
# Builds C++ + Kotlin/Native automatically if the binary is not found.
#
# Usage:
#   ./scripts/run.sh [server-url]
#   ./scripts/run.sh ws://localhost:9001
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILDS="$ROOT/builds/kotlin"

# ---------------------------------------------------------------------------
# Detect platform → KMP target name and binary extension
# ---------------------------------------------------------------------------
OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
    Linux)
        TARGET="linuxX64"
        EXT=".kexe"
        NATIVE_LIB="$ROOT/build/client/libdriscord_capi.so"
        ;;
    Darwin)
        if [ "$ARCH" = "arm64" ]; then
            TARGET="macosArm64"
        else
            TARGET="macosX64"
        fi
        EXT=".kexe"
        NATIVE_LIB="$ROOT/build/client/libdriscord_capi.dylib"
        ;;
    *)
        echo "ERROR: Unsupported OS: $OS. Use run.bat on Windows."
        exit 1
        ;;
esac

BINARY="$BUILDS/bin/$TARGET/driscordReleaseExecutable/driscord$EXT"

# ---------------------------------------------------------------------------
# Auto-build if binary or native lib is missing
# ---------------------------------------------------------------------------
if [ ! -f "$BINARY" ] || [ ! -f "$NATIVE_LIB" ]; then
    echo "==> Native binary not found — building..."
    bash "$(dirname "$0")/build_client.sh"
fi

if [ ! -f "$BINARY" ]; then
    echo "ERROR: Build succeeded but binary not found at:"
    echo "  $BINARY"
    exit 1
fi

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------
LIB_DIR="$(dirname "$NATIVE_LIB")"
echo "==> Launching Driscord (Kotlin/Native $TARGET)"
echo "    Binary : $BINARY"
echo "    LibDir : $LIB_DIR"

# Prepend the library directory to the dynamic loader search path
case "$OS" in
    Linux)  export LD_LIBRARY_PATH="${LIB_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ;;
    Darwin) export DYLD_LIBRARY_PATH="${LIB_DIR}${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" ;;
esac

exec "$BINARY" "$@"
