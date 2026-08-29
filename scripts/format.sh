#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

case "${1:-}" in
    "") MODE="format" ;;
    --check) MODE="check" ;;
    *)
        echo "Usage: $0 [--check]" >&2
        exit 2
        ;;
esac
if (( $# > 1 )); then
    echo "Usage: $0 [--check]" >&2
    exit 2
fi

mapfile -d '' -t FILES < <(
    find "$ROOT/core" "$ROOT/backend/signaling_server" "$ROOT/client-qt" \
        \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) \
        ! -path '*/.builds/*' ! -path '*/_deps/*' ! -path '*/build/*' \
        -print0 \
        | sort -z
)

if (( ${#FILES[@]} == 0 )); then
    echo "No files to format."
    exit 0
fi

if [[ "$MODE" == "check" ]]; then
    clang-format --dry-run --Werror "${FILES[@]}"
else
    clang-format -i "${FILES[@]}"
    echo "Formatted ${#FILES[@]} files."
fi
