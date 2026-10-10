#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build/remaining_loops"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
mkdir -p "$build"
common=(
    -std=gnu11 -O3 -Wall -Wextra -flto -flto-partition=one -fno-strict-aliasing
    -DPICOGB_CUSTOM=1 -DKNOVGBC_SATURN_CONFIG_TEST=1
    -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1
    -DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1
    -DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_F0_ROM_OPERAND_INLINE=1
    -DKNOVGBC_IDLE_SPIN=1 -DKNOVGBC_IDLE_EVENT=1 -DKNOVGBC_IDLE_RESUME=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
    "$repo/src/saturn/picogb_idle_spin.c"
    "$repo/tools/tests/picogb_remaining_loops.c"
)
for frames in 1800 3600; do
    extra=(-DKNOVGBC_SMOKE_LONG=1)
    if [[ "$frames" == 3600 ]]; then extra+=(-DKNOVGBC_SMOKE_GAMEPLAY_INPUT=1); fi
    "$compiler" "${common[@]}" "${extra[@]}" -o "$build/profile_${frames}" \
        2> "$build/profile_${frames}.compile.log"
    "$build/profile_${frames}" "$rom" > "$build/profile_${frames}.txt" \
        2> "$build/profile_${frames}.stats"
    cat "$build/profile_${frames}.txt" "$build/profile_${frames}.stats"
done
