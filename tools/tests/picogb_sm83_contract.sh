#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
mkdir -p "$build"

"$compiler" -std=gnu11 -O2 -Wall -Wextra \
    -DPICOGB_CUSTOM=1 \
    -I"$repo/tools/tests/picogb_include" \
    -I"$repo/third_party/PicoGB" \
    "$repo/tools/tests/picogb_sm83_contract.c" \
    "$repo/third_party/PicoGB/mi.c" \
    "$repo/third_party/PicoGB/microcode_dispatch.c" \
    "$repo/third_party/PicoGB/microcode.c" \
    "$repo/third_party/PicoGB/ppu.c" \
    "$repo/third_party/PicoGB/fabric.c" \
    -o "$build/picogb_sm83_contract"

"$build/picogb_sm83_contract"
