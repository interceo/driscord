#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 <driscord-client-*-linux-x86_64.tar.gz>" >&2
    exit 2
}

[[ $# -eq 1 ]] || usage
archive=$1
[[ -f "$archive" ]] || { echo "archive not found: $archive" >&2; exit 1; }

for tool in tar readelf ldd find grep sort mktemp; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "required tool not found: $tool" >&2
        exit 1
    }
done

cleanup_dir=$(mktemp -d)
trap 'rm -rf -- "$cleanup_dir"' EXIT
tar -xzf "$archive" -C "$cleanup_dir"

mapfile -t clients < <(find "$cleanup_dir" -type f -path '*/bin/driscord_client' -print)
if [[ ${#clients[@]} -ne 1 ]]; then
    echo "expected exactly one bin/driscord_client, found ${#clients[@]}" >&2
    exit 1
fi

client=${clients[0]}
package_root=${client%/bin/driscord_client}

required=(
    bin/driscord_client
    lib/libQt6Widgets.so.6
    lib/libQt6Core5Compat.so.6
    plugins/platforms/libqoffscreen.so
    plugins/platforms/libqminimal.so
)
for path in "${required[@]}"; do
    [[ -e "$package_root/$path" ]] || {
        echo "required runtime artifact is missing: $path" >&2
        exit 1
    }
done

compgen -G "$package_root/lib/libdatachannel.so*" >/dev/null || {
    echo "required runtime artifact is missing: lib/libdatachannel.so*" >&2
    exit 1
}

for path in include lib/cmake; do
    [[ ! -e "$package_root/$path" ]] || {
        echo "development files leaked into archive: $path" >&2
        exit 1
    }
done

mapfile -d '' -t elf_files < <(
    find "$package_root" -type f -print0 |
        while IFS= read -r -d '' candidate; do
            if readelf -h "$candidate" >/dev/null 2>&1; then
                printf '%s\0' "$candidate"
            fi
        done
)
[[ ${#elf_files[@]} -gt 0 ]] || {
    echo "archive contains no ELF files" >&2
    exit 1
}

dynamic=$(readelf -d "${elf_files[@]}" 2>/dev/null || true)
if grep -E '(RPATH|RUNPATH)' <<<"$dynamic" |
        grep -E '(/opt/qt|/source|/ci/|\.builds|/mnt/raid1)' >/dev/null; then
    echo "build path leaked into RPATH/RUNPATH" >&2
    grep -E 'File:|RPATH|RUNPATH' <<<"$dynamic" >&2
    exit 1
fi

client_dynamic=$(readelf -d "$client")
if ! grep -F 'Library runpath: [$ORIGIN:$ORIGIN/../lib]' \
        <<<"$client_dynamic" >/dev/null; then
    echo 'unexpected RUNPATH for bin/driscord_client' >&2
    grep -E '(RPATH|RUNPATH)' <<<"$client_dynamic" >&2 || true
    exit 1
fi

# ldd accepts multiple files and prints a heading before each result. Running
# one process for the whole package is dramatically faster than starting the
# dynamic loader separately for every Qt and QML plugin.
ldd_output=$(ldd "${elf_files[@]}" 2>&1) || {
    echo "ldd failed for one or more packaged ELF files" >&2
    echo "$ldd_output" >&2
    exit 1
}
if grep -F 'not found' <<<"$ldd_output" >/dev/null; then
    echo "unresolved dependency in package" >&2
    echo "$ldd_output" >&2
    exit 1
fi
if grep -E '(/opt/qt|/source|/ci/|\.builds|/mnt/raid1)' \
        <<<"$ldd_output" >/dev/null; then
    echo "build-host dependency leaked into package" >&2
    echo "$ldd_output" >&2
    exit 1
fi

version_info=$(readelf --version-info "${elf_files[@]}" 2>/dev/null || true)

version_max() {
    local prefix=$1
    local value
    value=$(
        printf '%s\n' "$version_info" |
            grep -oE "${prefix}_[0-9]+(\\.[0-9]+)+" |
            sort -Vu |
            tail -n 1
    )
    printf '%s\n' "$value"
}

version_exceeds() {
    local actual=$1
    local maximum=$2
    [[ "$(printf '%s\n%s\n' "$actual" "$maximum" | sort -V | tail -n 1)" != "$maximum" ]]
}

max_glibc=$(version_max GLIBC)
max_glibcxx=$(version_max GLIBCXX)
[[ -n "$max_glibc" ]] || { echo "no GLIBC symbol requirements found" >&2; exit 1; }
[[ -n "$max_glibcxx" ]] || { echo "no GLIBCXX symbol requirements found" >&2; exit 1; }

if version_exceeds "${max_glibc#GLIBC_}" 2.34; then
    echo "GLIBC floor exceeded: $max_glibc > GLIBC_2.34" >&2
    exit 1
fi
if version_exceeds "${max_glibcxx#GLIBCXX_}" 3.4.30; then
    echo "GLIBCXX floor exceeded: $max_glibcxx > GLIBCXX_3.4.30" >&2
    exit 1
fi

echo "package root: $package_root"
echo "ELF files checked: ${#elf_files[@]}"
echo "maximum symbol requirements: $max_glibc, $max_glibcxx"
echo "package check passed"
