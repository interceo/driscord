#!/usr/bin/env bash
# Format C/C++ source files with clang-format.
#
# Usage:
#   ./scripts/format.sh          # format all tracked files
#   ./scripts/format.sh --check  # dry-run, exit 1 on diff (used by pre-commit)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

MODE="format"
if [[ "${1:-}" == "--check" ]]; then
    MODE="check"
fi

FILES=$(find "$ROOT/core" "$ROOT/backend/signaling_server" \
    \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) \
    ! -path '*/.builds/*' ! -path '*/_deps/*')

if [[ -z "$FILES" ]]; then
    echo "No files to format."
    exit 0
fi

if [[ "$MODE" == "check" ]]; then
    echo "$FILES" | xargs clang-format --dry-run --Werror
else
    echo "$FILES" | xargs clang-format -i
    echo "Formatted $(echo "$FILES" | wc -l) files."
fi
