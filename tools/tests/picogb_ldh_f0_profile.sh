#!/usr/bin/env bash
# Profile the immediate high-address operand used by LDH A,(a8) (opcode F0)
# using the exact host configuration of the Master+VDP2/fetch baseline.
# This is diagnostic-only: KNOVGBC_LDH_F0_PROFILE is never enabled by a
# Saturn build target.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
mkdir -p "$build"

common=(
    -std=gnu11 -O3 -Wall -Wextra
    -flto -flto-partition=one -fno-strict-aliasing
    -DPICOGB_CUSTOM=1
    -DKNOVGBC_SATURN_CONFIG_TEST=1
    -DKNOVGBC_FAST_MEMORY=1
    -DKNOVGBC_PPU_PALETTE_CACHE=1
    -DKNOVGBC_OPCODE_FETCH_INLINE=1
    -DKNOVGBC_LDH_F0_PROFILE=1
    -I"$repo/src/saturn"
    -I"$repo/third_party/PicoGB"
    "$repo/tools/tests/picogb_smoke.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)

"$compiler" "${common[@]}" -o "$build/picogb_ldh_f0_profile"
"$build/picogb_ldh_f0_profile" "$rom" | tee "$build/picogb_ldh_f0_profile.txt"
