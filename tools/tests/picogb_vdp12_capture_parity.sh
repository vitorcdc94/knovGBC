#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
mkdir -p "$build"

common=(
    -std=gnu11 -O3 -Wall -Wextra
    -fno-strict-aliasing -DPICOGB_CUSTOM=1
    -DKNOVGBC_VDP2_INDEXED8=1
    -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1
    -DKNOVGBC_VDP12_NATIVE=1 -DKNOVGBC_VDP12_SOFTWARE_REFERENCE=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB"
    "$repo/tools/tests/picogb_vdp12_capture_parity.c"
    "$repo/src/saturn/picogb_vdp12_frame.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)

if [[ "${SANITIZE:-0}" == 1 ]]; then
    common+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi

"$compiler" "${common[@]}" -o "$build/picogb_vdp12_capture_parity"
"$build/picogb_vdp12_capture_parity"
