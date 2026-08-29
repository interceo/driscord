#!/usr/bin/env bash
set -euo pipefail

BUILD_TAG="${DRISCORD_BUILD_TAG:-}"
BUILD_DIR="${1:-.builds/${BUILD_TAG}coverage}"
PROFILE_DIR="${BUILD_DIR}/profiles"
OUT_DIR="${BUILD_DIR}/coverage"

if [[ ! -d "${PROFILE_DIR}" ]]; then
    echo "no profiles in ${PROFILE_DIR}; run the coverage preset first" >&2
    exit 1
fi

mkdir -p "${OUT_DIR}"

mapfile -t BINARIES < <(
    ctest --test-dir "${BUILD_DIR}" --show-only=json-v1 2>/dev/null \
        | python3 -c '
import json, sys
data = json.load(sys.stdin)
seen = set()
for test in data.get("tests", []):
    argv = test.get("command", [])
    if argv:
        exe = argv[0]
        if exe not in seen:
            seen.add(exe)
            print(exe)
'
)

if [[ ${#BINARIES[@]} -eq 0 ]]; then
    echo "no test binaries discovered under ${BUILD_DIR}" >&2
    exit 1
fi

llvm-profdata merge -sparse "${PROFILE_DIR}"/*.profraw \
    -o "${OUT_DIR}/merged.profdata"

OBJECT_ARGS=()
for binary in "${BINARIES[@]}"; do
    [[ -x "${binary}" ]] && OBJECT_ARGS+=(-object "${binary}")
done

llvm-cov report "${OBJECT_ARGS[@]}" \
    -instr-profile="${OUT_DIR}/merged.profdata" \
    -ignore-filename-regex='(_deps|/tests/|/usr/)' \
    | tee "${OUT_DIR}/summary.txt"

llvm-cov export "${OBJECT_ARGS[@]}" \
    -instr-profile="${OUT_DIR}/merged.profdata" \
    -format=lcov \
    -ignore-filename-regex='(_deps|/tests/|/usr/)' \
    > "${OUT_DIR}/coverage.lcov"

echo "coverage written to ${OUT_DIR}/{summary.txt,coverage.lcov}"
