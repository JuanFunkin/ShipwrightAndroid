#!/usr/bin/env bash
# Applies the Android port on top of a Ship of Harkinian (Shipwright) checkout.
#
#   scripts/apply.sh <path-to-shipwright-checkout>
#
# 1. patches/shipwright/*.patch     - changes to the game (build as a shared library, touch input hook, ...)
# 2. patches/libultraship/*.patch   - changes to libultraship (game folder on Android, GLES shader precision)
# 3. overlay/                       - new source files (on-screen controls), copied in
# 4. torch-overlay/                 - Torch's O2R writer replaced by one that handles more than 65535 files
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "usage: $0 <path-to-shipwright-checkout>" >&2
    exit 2
fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UP="$(cd "$1" && pwd)"

if [ ! -f "$UP/soh/CMakeLists.txt" ]; then
    echo "error: $UP does not look like a Shipwright checkout" >&2
    exit 1
fi

apply_patches() { # <git dir> <patch dir>
    for patch in "$2"/*.patch; do
        echo "    $(basename "$patch")"
        git -C "$1" apply --ignore-whitespace --check "$patch"
        git -C "$1" apply --ignore-whitespace "$patch"
    done
}

echo "==> Patching Ship of Harkinian"
apply_patches "$UP" "$HERE/patches/shipwright"

echo "==> Patching libultraship"
apply_patches "$UP/libultraship" "$HERE/patches/libultraship"

echo "==> Copying the touch controls"
cp -r "$HERE/overlay/." "$UP/"

echo "==> Replacing Torch's O2R writer (more than 65535 entries, streams to disk)"
cp -r "$HERE/torch-overlay/torch/." "$UP/torch/"

echo "==> Done"
