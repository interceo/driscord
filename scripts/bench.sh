#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-bench"

cmake -S "$ROOT" -B "$BUILD" -DBUILD_TESTS=ON -DBUILD_SERVER=OFF -DBUILD_CLIENT=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"

echo "=== bench_jitter ==="
"$BUILD/tests/bench_jitter"

echo ""
echo "=== bench_protocol ==="
"$BUILD/tests/bench_protocol"
