#!/usr/bin/env bash
# Compare 036 and 037 frame-by-frame, including gameplay inputs.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build/cp_n8"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
mkdir -p "$build"
common=(
    -std=gnu11 -O3 -Wall -Wextra -flto -flto-partition=one -fno-strict-aliasing
    -DPICOGB_CUSTOM=1 -DKNOVGBC_SATURN_CONFIG_TEST=1
    -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1
    -DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1
    -DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_F0_ROM_OPERAND_INLINE=1
    -DKNOVGBC_IDLE_SPIN=1 -DKNOVGBC_IDLE_EVENT=1
    -DKNOVGBC_IDLE_RESUME=1 -DKNOVGBC_IDLE_COUNTDOWN=1
    -DKNOVGBC_HRAM_WAIT=1 -DKNOVGBC_FRAME_TRACE=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
    "$repo/src/saturn/picogb_idle_spin.c"
    "$repo/src/saturn/picogb_idle_countdown.c"
    "$repo/src/saturn/picogb_hram_wait.c"
    "$repo/tools/tests/picogb_smoke.c"
)
for frames in 600 1800 3600; do
    extra=()
    if [[ "$frames" == 1800 ]]; then extra=(-DKNOVGBC_SMOKE_LONG=1); fi
    if [[ "$frames" == 3600 ]]; then
        extra=(-DKNOVGBC_SMOKE_LONG=1 -DKNOVGBC_SMOKE_GAMEPLAY_INPUT=1)
    fi
    "$compiler" "${common[@]}" "${extra[@]}" -o "$build/base_${frames}" \
        2> "$build/base_${frames}.compile.log"
    "$compiler" "${common[@]}" "${extra[@]}" -DKNOVGBC_CP_N8_DIRECT=1 \
        -o "$build/cp_${frames}" 2> "$build/cp_${frames}.compile.log"
    "$build/base_${frames}" "$rom" > "$build/base_${frames}.txt" \
        2> "$build/base_${frames}.stats"
    "$build/cp_${frames}" "$rom" > "$build/cp_${frames}.txt" \
        2> "$build/cp_${frames}.stats"
    diff -u "$build/base_${frames}.txt" "$build/cp_${frames}.txt"
    diff -u "$build/base_${frames}.stats" "$build/cp_${frames}.stats"
    tail -n 3 "$build/cp_${frames}.txt"
done
echo 'picogb_cp_n8_differential: PASS (each frame, cycles, state, memory and image)'
