#!/usr/bin/env bash
# Fetches the pinned Google WebRTC revision and builds its official complete
# static archive.  The checkout stays under .cache/ and is never vendored into
# the repository.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REVISION_FILE="$PROJECT_ROOT/third_party/google_webrtc_revision.txt"
DEPOT_TOOLS_REVISION_FILE="$PROJECT_ROOT/third_party/depot_tools_revision.txt"
WEBRTC_PATCHES=(
    "$PROJECT_ROOT/third_party/patches/google-webrtc-libstdcxx.patch"
    "$PROJECT_ROOT/third_party/patches/google-webrtc-build-graph.patch"
)
REVISION="$(tr -d '[:space:]' < "$REVISION_FILE")"
DEPOT_TOOLS_REVISION="$(tr -d '[:space:]' < "$DEPOT_TOOLS_REVISION_FILE")"
DEPOT_TOOLS_DIR="${DRISCORD_DEPOT_TOOLS_DIR:-$PROJECT_ROOT/.cache/depot_tools}"
CHECKOUT_ROOT="${DRISCORD_WEBRTC_CHECKOUT_ROOT:-$PROJECT_ROOT/.cache/google-webrtc}"
SOURCE_DIR="$CHECKOUT_ROOT/src"
# One checkout serves both targets; only the GN out directory and the exported
# SDK differ. The Windows cross build consumes a packaged MSVC+SDK layout in
# the package_from_installed.py format (as produced by `xwin splat
# --use-winsysroot-style --preserve-ms-arch-notation` plus the SetEnv json):
# GN resolves it through build/win_toolchain.json, clang through /winsysroot.
TARGET="${DRISCORD_WEBRTC_TARGET:-linux}"
case "$TARGET" in
linux)
    OUT_DIR="${DRISCORD_WEBRTC_OUT_DIR:-$SOURCE_DIR/out/driscord-release}"
    SDK_ROOT="${DRISCORD_WEBRTC_SDK_ROOT:-$PROJECT_ROOT/.cache/google-webrtc-sdk}"
    ;;
windows)
    OUT_DIR="${DRISCORD_WEBRTC_OUT_DIR:-$SOURCE_DIR/out/driscord-release-win}"
    SDK_ROOT="${DRISCORD_WEBRTC_SDK_ROOT:-$PROJECT_ROOT/.cache/google-webrtc-sdk-win}"
    MSVC_SYSROOT="${DRISCORD_MSVC_SYSROOT:?the windows target needs DRISCORD_MSVC_SYSROOT}"
    # GN and clang receive this path verbatim; keep it absolute.
    MSVC_SYSROOT="$(cd "$MSVC_SYSROOT" && pwd)"
    for probe in "VC/Tools/MSVC" "Windows Kits/10/bin/SetEnv.x64.json"; do
        if [ ! -e "$MSVC_SYSROOT/$probe" ]; then
            echo "ERROR: $MSVC_SYSROOT is not a packaged MSVC sysroot:" \
                "$probe is missing" >&2
            exit 1
        fi
    done
    ;;
*)
    echo "ERROR: DRISCORD_WEBRTC_TARGET must be 'linux' or 'windows'," \
        "got '$TARGET'" >&2
    exit 1
    ;;
esac
# gclient defaults to max(8, nproc) concurrent clones. On a many-core machine
# that reliably trips googlesource's anonymous rate limit (HTTP 429,
# "shared_anonymous") partway through a first sync.
GCLIENT_JOBS="${DRISCORD_GCLIENT_JOBS:-8}"

if [ ! -x "$DEPOT_TOOLS_DIR/gclient" ]; then
    echo "==> Fetching depot_tools..."
    git clone --no-checkout --filter=blob:none \
        https://chromium.googlesource.com/chromium/tools/depot_tools.git \
        "$DEPOT_TOOLS_DIR"
fi

if [ ! -d "$DEPOT_TOOLS_DIR/.git" ]; then
    echo "ERROR: depot_tools at $DEPOT_TOOLS_DIR is not a git checkout" >&2
    exit 1
fi
if ! git -C "$DEPOT_TOOLS_DIR" cat-file -e \
    "$DEPOT_TOOLS_REVISION^{commit}" 2>/dev/null; then
    git -C "$DEPOT_TOOLS_DIR" fetch --depth=1 origin \
        "$DEPOT_TOOLS_REVISION"
fi
git -C "$DEPOT_TOOLS_DIR" checkout --detach --force \
    "$DEPOT_TOOLS_REVISION"
export DEPOT_TOOLS_UPDATE=0
# gclient shells out to helpers (cipd, vpython3) that resolve through PATH.
export PATH="$DEPOT_TOOLS_DIR:$PATH"

if [ ! -f "$CHECKOUT_ROOT/.gclient" ]; then
    echo "==> Creating Google WebRTC checkout..."
    mkdir -p "$CHECKOUT_ROOT"
    (
        cd "$CHECKOUT_ROOT"
        "$DEPOT_TOOLS_DIR/fetch" --nohooks webrtc
    )
fi

# Production builds do not need Chromium's instrumented sysroot libraries.
# Re-create the tiny client config deterministically so repeated builds do not
# inherit machine-global gclient defaults.
(
    cd "$CHECKOUT_ROOT"
    "$DEPOT_TOOLS_DIR/gclient" config \
        --name=src \
        --custom-var=checkout_configuration=small \
        https://webrtc.googlesource.com/src.git
)
# `gclient config` rewrites .gclient, so the target_os line never duplicates.
# The win entry only adds deps (cross clang runtime); a later linux run does
# not need to prune them.
if [ "$TARGET" = windows ]; then
    printf 'target_os = ["win"]\n' >> "$CHECKOUT_ROOT/.gclient"
fi

# Leave the checkout clean while gclient moves between revisions.
if [ -d "$SOURCE_DIR/.git" ]; then
    for patch in "${WEBRTC_PATCHES[@]}"; do
        if git -C "$SOURCE_DIR" apply --reverse --check "$patch" 2>/dev/null; then
            git -C "$SOURCE_DIR" apply --reverse "$patch"
        fi
    done
fi

echo "==> Syncing Google WebRTC at $REVISION..."
(
    cd "$CHECKOUT_ROOT"
    # WebRTC's unconditional resource hook downloads gigabytes of test media.
    # Production //:webrtc with rtc_include_tests=false does not consume it.
    "$DEPOT_TOOLS_DIR/gclient" sync --nohooks --revision "src@$REVISION" \
        -j"$GCLIENT_JOBS"
)

ACTUAL_REVISION="$(git -C "$SOURCE_DIR" rev-parse HEAD)"
if [ "$ACTUAL_REVISION" != "$REVISION" ]; then
    echo "ERROR: expected WebRTC $REVISION, got $ACTUAL_REVISION" >&2
    exit 1
fi

# Driscord uses libstdc++ on Linux so that the GN archive and the surrounding
# CMake/Qt code share one C++ ABI. Keep the small compatibility/build-graph
# delta for this exact revision explicit and repeatable.
for patch in "${WEBRTC_PATCHES[@]}"; do
    git -C "$SOURCE_DIR" apply --check "$patch"
    git -C "$SOURCE_DIR" apply "$patch"
done

# This is the only generated hook output referenced by the production GN
# graph. A host sysroot is used below, so Chromium's Debian sysroot hook is not
# needed either.
python3 "$SOURCE_DIR/build/util/lastchange.py" \
    -o "$SOURCE_DIR/build/util/LASTCHANGE"

if [ "$TARGET" = windows ]; then
    # vs_toolchain.py never probes a Linux host for Visual Studio: an existing
    # win_toolchain.json whose version matches its packaged expectation
    # short-circuits the (Google-internal) toolchain download and points GN at
    # the sysroot. setup_toolchain.py then loads
    # "Windows Kits/10/bin/SetEnv.x64.json" from it and hands clang and
    # lld-link a plain /winsysroot.
    python3 - "$SOURCE_DIR" "$MSVC_SYSROOT" <<'EOF'
import json
import sys

source_dir, sysroot = sys.argv[1:3]
sys.path.insert(0, source_dir + "/build")
import vs_toolchain

with open(source_dir + "/build/win_toolchain.json", "w") as f:
    json.dump(
        {
            "path": sysroot,
            "version": vs_toolchain.GetVisualStudioVersion(),
            "win_sdk": sysroot + "/Windows Kits/10",
            "wdk": "",
            "runtime_dirs": [],
        },
        f,
    )
EOF
fi

GN_ARGS=$(cat <<'EOF'
is_debug=false
is_component_build=false
rtc_include_tests=false
rtc_build_examples=false
rtc_build_tools=false
rtc_enable_protobuf=false
treat_warnings_as_errors=false
use_custom_libcxx=false
use_sysroot=false
symbol_level=0
EOF
)

if [ "$TARGET" = windows ]; then
    GN_ARGS="$GN_ARGS
target_os=\"win\"
target_cpu=\"x64\""
fi

echo "==> Generating GN build..."
"$SOURCE_DIR/buildtools/linux64/gn" gen "$OUT_DIR" \
    --root="$SOURCE_DIR" --args="$GN_ARGS"

echo "==> Building //:webrtc..."
"$SOURCE_DIR/third_party/ninja/ninja" -C "$OUT_DIR" webrtc

if [ "$TARGET" = windows ]; then
    ARCHIVE="$OUT_DIR/obj/webrtc.lib"
else
    ARCHIVE="$OUT_DIR/obj/libwebrtc.a"
fi
if [ ! -f "$ARCHIVE" ]; then
    echo "ERROR: GN completed but $ARCHIVE was not produced" >&2
    exit 1
fi

echo "==> Google WebRTC ready: $ARCHIVE"
echo "    CMake: -DDRISCORD_USE_GOOGLE_WEBRTC=ON"

# GitHub's cache is not large enough for a full Chromium checkout. Export the
# compile-time surface that Driscord actually consumes: headers, generated
# headers and the complete static archive. Keeping original relative paths
# means the same imported CMake target works against a checkout or this SDK.
SDK_STAGING="$SDK_ROOT.staging"
cmake -E remove_directory "$SDK_STAGING"
cmake -E make_directory "$SDK_STAGING/src"
(
    cd "$SOURCE_DIR"
    find . -type f \
        \( -name '*.h' -o -name '*.hpp' -o -name '*.inc' \
        -o -name '*.inl' -o -name '*.def' \) -print0 \
        | tar --null --files-from=- -cf - \
        | tar -xf - -C "$SDK_STAGING/src"
)
OUT_REL="${OUT_DIR#"$SOURCE_DIR/"}"
if [ "$OUT_REL" = "$OUT_DIR" ]; then
    echo "ERROR: the SDK export expects OUT_DIR under $SOURCE_DIR" >&2
    exit 1
fi
cmake -E make_directory "$SDK_STAGING/src/$OUT_REL/obj"
cmake -E copy "$ARCHIVE" \
    "$SDK_STAGING/src/$OUT_REL/obj/$(basename "$ARCHIVE")"
if [ "$TARGET" = windows ]; then
    # GN links the archive's compiler-rt intrinsics from the Chromium clang
    # package; a consumer's clang does not ship Windows builtins, so the SDK
    # carries them next to the archive.
    BUILTINS="$(echo "$SOURCE_DIR"/third_party/llvm-build/Release+Asserts/lib/clang/*/lib/windows/clang_rt.builtins-x86_64.lib)"
    if [ ! -f "$BUILTINS" ]; then
        echo "ERROR: clang_rt.builtins-x86_64.lib not found in the checkout" >&2
        exit 1
    fi
    cmake -E copy "$BUILTINS" \
        "$SDK_STAGING/src/$OUT_REL/obj/clang_rt.builtins-x86_64.lib"
fi
cmake -E remove_directory "$SDK_ROOT"
cmake -E rename "$SDK_STAGING" "$SDK_ROOT"
echo "==> Cacheable WebRTC SDK ready: $SDK_ROOT"
