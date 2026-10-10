#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
mkdir -p "$build"

"$compiler" -std=gnu11 -O2 -Wall -Wextra \
    -DPICOGB_CUSTOM=1 \
    -I"$repo/tools/tests/picogb_include" \
    -I"$repo/third_party/PicoGB" \
    "$repo/tools/tests/picogb_smoke.c" \
    "$repo/third_party/PicoGB/mi.c" \
    "$repo/third_party/PicoGB/microcode_dispatch.c" \
    "$repo/third_party/PicoGB/microcode.c" \
    "$repo/third_party/PicoGB/ppu.c" \
    "$repo/third_party/PicoGB/fabric.c" \
    -o "$build/picogb_smoke"

"$build/picogb_smoke" "$rom"
