#!/usr/bin/env bash
# Source-based coverage for the C++ test suite.
#
# Builds nothing: it consumes the .profraw files that the `coverage` test
# preset writes (LLVM_PROFILE_FILE points there) and produces a merged
# llvm-cov summary plus an lcov-style export for CI to archive.
#
# Usage:
#   cmake --workflow --preset coverage      # produces profiles + binaries
#   scripts/coverage_report.sh              # merges and reports
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

# The test binaries whose coverage we care about — the ones exercising
# production code. Discovered from the CTest registration so the list does not
# drift from the build.
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

# Human-readable summary to stdout (and the CI log).
llvm-cov report "${OBJECT_ARGS[@]}" \
    -instr-profile="${OUT_DIR}/merged.profdata" \
    -ignore-filename-regex='(_deps|/tests/|/usr/)' \
    | tee "${OUT_DIR}/summary.txt"

# lcov export for archival / diffing.
llvm-cov export "${OBJECT_ARGS[@]}" \
    -instr-profile="${OUT_DIR}/merged.profdata" \
    -format=lcov \
    -ignore-filename-regex='(_deps|/tests/|/usr/)' \
    > "${OUT_DIR}/coverage.lcov"

echo "coverage written to ${OUT_DIR}/{summary.txt,coverage.lcov}"
