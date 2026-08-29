#!/usr/bin/env bash
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
export PATH="$DEPOT_TOOLS_DIR:$PATH"

if [ ! -f "$CHECKOUT_ROOT/.gclient" ]; then
    echo "==> Creating Google WebRTC checkout..."
    mkdir -p "$CHECKOUT_ROOT"
    (
        cd "$CHECKOUT_ROOT"
        "$DEPOT_TOOLS_DIR/fetch" --nohooks webrtc
    )
fi

(
    cd "$CHECKOUT_ROOT"
    "$DEPOT_TOOLS_DIR/gclient" config \
        --name=src \
        --custom-var=checkout_configuration=small \
        https://webrtc.googlesource.com/src.git
)
if [ "$TARGET" = windows ]; then
    printf 'target_os = ["win"]\n' >> "$CHECKOUT_ROOT/.gclient"
fi

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
    "$DEPOT_TOOLS_DIR/gclient" sync --nohooks --revision "src@$REVISION" \
        -j"$GCLIENT_JOBS"
)

ACTUAL_REVISION="$(git -C "$SOURCE_DIR" rev-parse HEAD)"
if [ "$ACTUAL_REVISION" != "$REVISION" ]; then
    echo "ERROR: expected WebRTC $REVISION, got $ACTUAL_REVISION" >&2
    exit 1
fi

for patch in "${WEBRTC_PATCHES[@]}"; do
    git -C "$SOURCE_DIR" apply --check "$patch"
    git -C "$SOURCE_DIR" apply "$patch"
done

python3 "$SOURCE_DIR/build/util/lastchange.py" \
    -o "$SOURCE_DIR/build/util/LASTCHANGE"

if [ "$TARGET" = windows ]; then
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
