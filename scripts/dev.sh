#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Build server if not built
if [[ ! -x "$ROOT_DIR/build/server/driscord_server" ]]; then
  cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build"
  cmake --build "$ROOT_DIR/build" -j
fi

# Run server
DRISCORD_PORT=8080 "$ROOT_DIR/build/server/driscord_server" &
SERVER_PID=$!

# Run client
pushd "$ROOT_DIR/client" >/dev/null
npm i
npm run dev &
CLIENT_PID=$!
popd >/dev/null

cleanup() {
  kill $SERVER_PID || true
  kill $CLIENT_PID || true
}
trap cleanup EXIT

wait 