#!/usr/bin/env bash
# NixOS entry point for the pinned Google WebRTC artifact.
#
# scripts/build_google_webrtc.sh builds //:webrtc with the Chromium clang that
# ships inside the checkout, under use_sysroot=false and use_custom_libcxx=false.
# Both settings make that compiler resolve libstdc++, glibc and the crt objects
# from the host, which on a distribution without /usr/include finds nothing:
# the nixpkgs cc wrapper injects those paths, and the pinned clang is not the
# wrapper. This script derives the wrapper's own directories and hands them to
# the bare compiler through the standard driver environment variables, leaving
# the pinned toolchain (including the Chromium clang plugins) untouched.
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

# Header directories: the wrapper's entire search list, which is libstdc++,
# glibc and every package in nix/dev-shell.nix. WebRTC's Linux build expects
# ALSA, PulseAudio and X11 headers to sit in system directories the way they do
# on a FHS distribution. Its own headers stay ahead of these, because the
# driver appends the environment's directories after every command-line -I.
#
# The wrapper's resource-root is deliberately dropped: those are clang 21's
# builtin headers and must not be fed to the pinned clang 23.
search_dirs="$(clang++ -E -x c++ -v /dev/null 2>&1 \
    | sed -n '/#include <...> search starts here/,/End of search list/p' \
    | sed 's/^ //' \
    | grep -E '^/' \
    | grep -v 'resource-root')"
cxx_include_dirs="$(printf '%s\n' "$search_dirs" \
    | grep -E '/include/c\+\+/' | paste -sd:)"
# The libstdc++ directories must stay out of the C search path: libstdc++ ships
# a C++-only <stdatomic.h> that otherwise shadows clang's builtin one and breaks
# every C dependency using atomics (dav1d).
host_include_dirs="$(printf '%s\n' "$search_dirs" \
    | grep -vE '/include/c\+\+/' | paste -sd:)"

# Library directories: -L is honoured through LIBRARY_PATH, but crt objects are
# resolved from the driver's prefix list instead, which only COMPILER_PATH (or
# -B) feeds. Both are needed; neither alone links.
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

# gclient defaults to max(8, nproc) concurrent clones, which trips
# googlesource's anonymous rate limit (HTTP 429) on a many-core machine.
export DRISCORD_GCLIENT_JOBS="${DRISCORD_GCLIENT_JOBS:-4}"

echo "==> NixOS host toolchain for the pinned Chromium clang:"
echo "    CPLUS_INCLUDE_PATH=$CPLUS_INCLUDE_PATH"
echo "    LIBRARY_PATH=$LIBRARY_PATH"
echo "    COMPILER_PATH=$COMPILER_PATH"

exec "$ROOT/scripts/build_google_webrtc.sh" "$@"
