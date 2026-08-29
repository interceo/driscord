#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ "${DRISCORD_NIXOS_ENV:-}" != "1" ]; then
    command -v nix >/dev/null || {
        echo "ERROR: nix is required for scripts/nixos-webrtc.sh." >&2
        exit 1
    }
    exec nix develop "$ROOT" -c bash "$0" "$@"
fi

command -v clang++ >/dev/null || {
    echo "ERROR: clang++ is not on PATH; run inside the dev shell." >&2
    exit 1
}

search_dirs="$(clang++ -E -x c++ -v /dev/null 2>&1 \
    | sed -n '/#include <...> search starts here/,/End of search list/p' \
    | sed 's/^ //' \
    | grep -E '^/' \
    | grep -v 'resource-root')"
cxx_include_dirs="$(printf '%s\n' "$search_dirs" \
    | grep -E '/include/c\+\+/' | paste -sd:)"
host_include_dirs="$(printf '%s\n' "$search_dirs" \
    | grep -vE '/include/c\+\+/' | paste -sd:)"

glibc_lib_dir="$(dirname "$(clang++ -print-file-name=crt1.o)")"
gcc_crt_dir="$(dirname "$(clang++ -print-file-name=crtbegin.o)")"

clang_wrapper="$(dirname "$(dirname "$(command -v clang++)")")"
if [ ! -r "$clang_wrapper/nix-support/cc-ldflags" ]; then
    clang_wrapper="$(dirname "$(dirname "$(readlink -f "$(command -v clang++)")")")"
fi
if [ ! -r "$clang_wrapper/nix-support/cc-ldflags" ]; then
    echo "ERROR: cannot locate the cc wrapper's cc-ldflags next to clang++" >&2
    exit 1
fi

lib_dirs="$glibc_lib_dir"
for flag in $(cat "$clang_wrapper/nix-support/cc-ldflags"); do
    case "$flag" in
        -L*) lib_dirs="$lib_dirs:${flag#-L}" ;;
    esac
done

if [ -z "$cxx_include_dirs" ] || [ -z "$host_include_dirs" ]; then
    echo "ERROR: could not locate host libstdc++/glibc headers via clang++" >&2
    exit 1
fi

export C_INCLUDE_PATH="$host_include_dirs${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
export CPLUS_INCLUDE_PATH="$cxx_include_dirs:$host_include_dirs${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
export LIBRARY_PATH="$lib_dirs${LIBRARY_PATH:+:$LIBRARY_PATH}"
export COMPILER_PATH="$glibc_lib_dir:$gcc_crt_dir${COMPILER_PATH:+:$COMPILER_PATH}"

export DRISCORD_GCLIENT_JOBS="${DRISCORD_GCLIENT_JOBS:-4}"

echo "==> NixOS host toolchain for the pinned Chromium clang:"
echo "    CPLUS_INCLUDE_PATH=$CPLUS_INCLUDE_PATH"
echo "    LIBRARY_PATH=$LIBRARY_PATH"
echo "    COMPILER_PATH=$COMPILER_PATH"

exec "$ROOT/scripts/build_google_webrtc.sh" "$@"
