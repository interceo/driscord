#!/usr/bin/env bash
# Full Driscord build: signaling server + Compose client (Linux; optionally Windows).
#
# Usage:
#   ./scripts/build.sh            # Linux only
#   ./scripts/build.sh --windows  # also cross-compile Windows JNI DLL
set -euo pipefail

SCRIPTS="$(cd "$(dirname "$0")" && pwd)"

echo "========================================"
echo " Driscord full build"
echo "========================================"
echo ""

echo "--- [1/2] Signaling server ---"
bash "$SCRIPTS/build_server.sh"

echo ""
echo "--- [2/2] Compose client ---"
bash "$SCRIPTS/build_client.sh" "$@"

echo ""
echo "========================================"
echo " Build complete"
echo "   Server : builds/server/driscord_server"
echo "   Client : builds/client-compose/"
echo "========================================"
