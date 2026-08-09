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
OUT_DIR="${DRISCORD_WEBRTC_OUT_DIR:-$SOURCE_DIR/out/driscord-release}"
SDK_ROOT="${DRISCORD_WEBRTC_SDK_ROOT:-$PROJECT_ROOT/.cache/google-webrtc-sdk}"

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
    "$DEPOT_TOOLS_DIR/gclient" sync --nohooks --revision "src@$REVISION"
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

echo "==> Generating GN build..."
"$SOURCE_DIR/buildtools/linux64/gn" gen "$OUT_DIR" \
    --root="$SOURCE_DIR" --args="$GN_ARGS"

echo "==> Building //:webrtc..."
"$SOURCE_DIR/third_party/ninja/ninja" -C "$OUT_DIR" webrtc

ARCHIVE="$OUT_DIR/obj/libwebrtc.a"
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
cmake -E make_directory "$SDK_STAGING/src/out/driscord-release/obj"
cmake -E copy "$ARCHIVE" \
    "$SDK_STAGING/src/out/driscord-release/obj/libwebrtc.a"
cmake -E remove_directory "$SDK_ROOT"
cmake -E rename "$SDK_STAGING" "$SDK_ROOT"
echo "==> Cacheable WebRTC SDK ready: $SDK_ROOT"
