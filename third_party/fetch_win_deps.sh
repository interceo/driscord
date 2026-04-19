#!/usr/bin/env bash
# Downloads third-party prebuilt packages needed for cross-compiling the
# Windows client via cmake/toolchain-mingw64.cmake.
#
# Layout produced under third_party/windows/:
#   ffmpeg/   headers/.dll.a import libs/FFmpeg runtime DLLs
#             (BtbN LGPL shared build — minimal dep closure, ~30 MB of DLLs)
#   openssl/  headers/.a static libs  (MSYS2)
#
# Update the versions below when newer upstream packages are released:
#   https://github.com/BtbN/FFmpeg-Builds/releases
#   https://packages.msys2.org/package/mingw-w64-x86_64-openssl
#
# Usage:
#   ./third_party/fetch_win_deps.sh           # skip if already present
#   ./third_party/fetch_win_deps.sh --force   # re-download and re-extract
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CACHE_DIR="$SCRIPT_DIR/.cache"

MSYS2_BASE="https://mirror.msys2.org/mingw/mingw64"
OPENSSL_PKG="mingw-w64-x86_64-openssl-3.3.2-1-any.pkg.tar.zst"

# BtbN's rolling "latest" release is updated continuously — pin by tag if
# reproducibility matters. LGPL shared build has a tiny runtime-dep closure
# (just libwinpthread-1.dll from MinGW); GPL variant pulls in x264/x265 DLLs.
FFMPEG_URL="https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-lgpl-shared.zip"

FORCE=false
for arg in "$@"; do [ "$arg" = "--force" ] && FORCE=true; done

# ---------------------------------------------------------------------------
# Detect extraction tools
# ---------------------------------------------------------------------------
if command -v bsdtar &>/dev/null; then
    EXTRACT_ZST=(bsdtar -xf)
elif command -v tar &>/dev/null && tar --version 2>/dev/null | grep -q GNU && command -v zstd &>/dev/null; then
    EXTRACT_ZST=(tar -xf)
else
    echo "ERROR: need bsdtar (libarchive) or GNU tar + zstd to extract .tar.zst" >&2
    echo "       Arch: pacman -S libarchive   or   pacman -S zstd" >&2
    exit 1
fi

if ! command -v unzip &>/dev/null; then
    echo "ERROR: 'unzip' required for FFmpeg archive" >&2
    exit 1
fi

mkdir -p "$CACHE_DIR"

# ---------------------------------------------------------------------------
# MSYS2 package → third_party/windows/<name>/
# ---------------------------------------------------------------------------
fetch_msys2_pkg() {
    local pkg="$1"        # e.g. mingw-w64-x86_64-openssl-...pkg.tar.zst
    local dest_name="$2"  # e.g. openssl

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
    "${EXTRACT_ZST[@]}" "$archive" -C "$tmp"

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
    echo "    libs:    $(ls "$dest/lib/"*.{a,dll.a} 2>/dev/null | wc -l) static/import libraries"
    echo "    dlls:    $(ls "$dest/bin/"*.dll  2>/dev/null | wc -l) runtime DLLs"
}

# ---------------------------------------------------------------------------
# BtbN FFmpeg (zip) → third_party/windows/ffmpeg/
# ---------------------------------------------------------------------------
fetch_btbn_ffmpeg() {
    local dest="$SCRIPT_DIR/windows/ffmpeg"
    local archive="$CACHE_DIR/ffmpeg-btbn-lgpl-shared.zip"

    if [ -d "$dest/lib" ] && [ -d "$dest/include" ] && [ -d "$dest/bin" ] && ! $FORCE; then
        echo "==> ffmpeg: already present (pass --force to re-download)"
        return 0
    fi

    if [ ! -f "$archive" ] || $FORCE; then
        echo "==> Downloading BtbN FFmpeg (LGPL shared) ..."
        curl -fL --progress-bar -o "$archive" "$FFMPEG_URL"
    fi

    echo "==> Extracting FFmpeg ..."
    local tmp="$CACHE_DIR/_extract_ffmpeg"
    rm -rf "$tmp" && mkdir -p "$tmp"
    unzip -q "$archive" -d "$tmp"

    # Archive layout: ffmpeg-master-latest-win64-lgpl-shared/{bin,include,lib}/
    local inner
    inner=$(ls "$tmp" | head -1)
    if [ -z "$inner" ] || [ ! -d "$tmp/$inner/bin" ]; then
        echo "ERROR: unexpected archive layout in BtbN FFmpeg zip" >&2
        exit 1
    fi

    rm -rf "$dest" && mkdir -p "$dest"
    cp -r "$tmp/$inner/include" "$dest/"
    cp -r "$tmp/$inner/lib"     "$dest/"
    cp -r "$tmp/$inner/bin"     "$dest/"

    rm -rf "$tmp"
    echo "    headers: $(ls "$dest/include" 2>/dev/null | tr '\n' ' ')"
    echo "    libs:    $(ls "$dest/lib/"*.dll.a 2>/dev/null | wc -l) import libraries"
    echo "    dlls:    $(ls "$dest/bin/"*.dll  2>/dev/null | wc -l) runtime DLLs"
}

fetch_btbn_ffmpeg
echo ""
fetch_msys2_pkg "$OPENSSL_PKG" "openssl"
echo ""
echo "==> Done. third_party/windows/ is ready for cmake/toolchain-mingw64.cmake"
