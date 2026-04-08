#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="Debug"

for arg in "$@"; do
    case "$arg" in
        --release) BUILD_TYPE="Release" ;;
        --debug)   BUILD_TYPE="Debug" ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-test"

cmake -S "$ROOT" -B "$BUILD" -DBUILD_TESTS=ON -DBUILD_SERVER=OFF -DBUILD_CLIENT=OFF -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD" -j"$(nproc)"

cd "$BUILD"
ctest --output-on-failure
