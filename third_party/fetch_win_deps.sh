#!/usr/bin/env bash
# Downloads MinGW-w64 prebuilt packages from the MSYS2 repository into
# third_party/windows/{ffmpeg,openssl}/ — used by cmake/toolchain-mingw64.cmake.
#
# Packages fetched:
#   mingw-w64-x86_64-ffmpeg   → include/ lib/*.dll.a  bin/*.dll
#   mingw-w64-x86_64-openssl  → include/ lib/*.dll.a  bin/*.dll
#
# Update the versions below when newer MSYS2 packages are released:
#   https://packages.msys2.org/package/mingw-w64-x86_64-ffmpeg
#   https://packages.msys2.org/package/mingw-w64-x86_64-openssl
#
# Usage:
#   ./third_party/fetch_win_deps.sh           # skip if already present
#   ./third_party/fetch_win_deps.sh --force   # re-download and re-extract
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CACHE_DIR="$SCRIPT_DIR/.cache"
MSYS2_BASE="https://mirror.msys2.org/mingw/mingw64"

FFMPEG_PKG="mingw-w64-x86_64-ffmpeg-7.1.1-2-any.pkg.tar.zst"
OPENSSL_PKG="mingw-w64-x86_64-openssl-3.3.2-1-any.pkg.tar.zst"

FORCE=false
for arg in "$@"; do [ "$arg" = "--force" ] && FORCE=true; done

# ---------------------------------------------------------------------------
# Detect extraction tool
# ---------------------------------------------------------------------------
if command -v bsdtar &>/dev/null; then
    EXTRACT_CMD=(bsdtar -xf)
elif command -v tar &>/dev/null && tar --version 2>/dev/null | grep -q GNU && command -v zstd &>/dev/null; then
    EXTRACT_CMD=(tar -xf)
else
    echo "ERROR: need bsdtar (libarchive) or GNU tar + zstd to extract .tar.zst" >&2
    echo "       Arch: pacman -S libarchive   or   pacman -S zstd" >&2
    exit 1
fi

mkdir -p "$CACHE_DIR"

# ---------------------------------------------------------------------------
# Helper: download + extract one MSYS2 package into third_party/windows/<name>/
# ---------------------------------------------------------------------------
fetch_pkg() {
    local pkg="$1"        # e.g. mingw-w64-x86_64-ffmpeg-7.1.1-2-any.pkg.tar.zst
    local dest_name="$2"  # e.g. ffmpeg

    local dest="$SCRIPT_DIR/windows/$dest_name"
    local archive="$CACHE_DIR/$pkg"

    if [ -d "$dest/lib" ] && [ -d "$dest/include" ] && ! $FORCE; then
        echo "==> $dest_name: already present (pass --force to re-download)"
        return 0
    fi

    if [ ! -f "$archive" ] || $FORCE; then
        echo "==> Downloading $pkg ..."
        curl -fL --progress-bar -o "$archive" "$MSYS2_BASE/$pkg"
    fi

    echo "==> Extracting $pkg ..."
    local tmp="$CACHE_DIR/_extract_$dest_name"
    rm -rf "$tmp" && mkdir -p "$tmp"
    "${EXTRACT_CMD[@]}" "$archive" -C "$tmp"

    local mingw="$tmp/mingw64"
    if [ ! -d "$mingw" ]; then
        echo "ERROR: expected 'mingw64/' inside $pkg" >&2
        exit 1
    fi

    rm -rf "$dest" && mkdir -p "$dest"
    [ -d "$mingw/include" ] && cp -r "$mingw/include" "$dest/"
    [ -d "$mingw/lib"     ] && cp -r "$mingw/lib"     "$dest/"
    [ -d "$mingw/bin"     ] && cp -r "$mingw/bin"     "$dest/"

    rm -rf "$tmp"
    echo "    headers: $(ls "$dest/include" 2>/dev/null | tr '\n' ' ')"
    echo "    libs:    $(ls "$dest/lib/"*.dll.a 2>/dev/null | wc -l) import libraries"
    echo "    dlls:    $(ls "$dest/bin/"*.dll  2>/dev/null | wc -l) runtime DLLs"
}

fetch_pkg "$FFMPEG_PKG"  "ffmpeg"
echo ""
fetch_pkg "$OPENSSL_PKG" "openssl"
echo ""
echo "==> Done. third_party/windows/ is ready for cmake/toolchain-mingw64.cmake"
