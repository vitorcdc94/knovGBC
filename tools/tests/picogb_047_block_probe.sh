#!/usr/bin/env bash
# Compare the 047 core with the portable predecoded-block executor. Keep all
# existing 047 shortcuts in both builds and include sustained gameplay input.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
build="$repo/build/profile_047_block"
mkdir -p "$build"

common=(
    -std=gnu11 -O3 -Wall -Wextra -flto -flto-partition=one
    -fno-strict-aliasing -DPICOGB_CUSTOM=1
    -DKNOVGBC_SATURN_CONFIG_TEST=1 -DKNOVGBC_FAST_MEMORY=1
    -DKNOVGBC_PPU_PALETTE_CACHE=1 -DKNOVGBC_OPCODE_FETCH_INLINE=1
    -DKNOVGBC_LDH_HRAM_DIRECT=1 -DKNOVGBC_A7_DIRECT=1
    -DKNOVGBC_F0_ROM_OPERAND_INLINE=1 -DKNOVGBC_IDLE_SPIN=1
    -DKNOVGBC_IDLE_EVENT=1 -DKNOVGBC_IDLE_RESUME=1
    -DKNOVGBC_IDLE_COUNTDOWN=1 -DKNOVGBC_HRAM_WAIT=1
    -DKNOVGBC_PPU_EVENT_COALESCE=1 -DKNOVGBC_PPU_TWO_LINES=1
    -DKNOVGBC_PPU_ADAPTIVE_BATCH=1 -DKNOVGBC_VDP2_DMA_DIRECT=1
    -DKNOVGBC_SMPC_NONBLOCKING=1 -DKNOVGBC_VDP2_INDEXED8=1
    -DKNOVGBC_FRAME_TRACE=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB"
    "$repo/tools/tests/picogb_smoke.c"
    "$repo/src/saturn/picogb_idle_spin.c"
    "$repo/src/saturn/picogb_idle_countdown.c"
    "$repo/src/saturn/picogb_hram_wait.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)

for frames in 600 1800 3600; do
    extra=()
    if [[ "$frames" == 1800 ]]; then
        extra=(-DKNOVGBC_SMOKE_LONG=1)
    elif [[ "$frames" == 3600 ]]; then
        extra=(-DKNOVGBC_SMOKE_LONG=1 -DKNOVGBC_SMOKE_GAMEPLAY_INPUT=1)
    fi

    "$compiler" "${common[@]}" "${extra[@]}" \
        -o "$build/reference_$frames" \
        2> "$build/reference_$frames.compile.log"
    "$compiler" "${common[@]}" "${extra[@]}" \
        -DKNOVGBC_BLOCK_STATIC=1 -DKNOVGBC_BLOCK_METRICS=1 \
        -DKNOVGBC_BLOCK_IMMUTABLE_WINDOW=1 \
        "$repo/src/saturn/picogb_block_executor.c" \
        -o "$build/candidate_$frames" \
        2> "$build/candidate_$frames.compile.log"

    "$build/reference_$frames" "$rom" > "$build/reference_$frames.txt" \
        2> "$build/reference_$frames.stats"
    "$build/candidate_$frames" "$rom" > "$build/candidate_$frames.txt" \
        2> "$build/candidate_$frames.stats"

    diff -u "$build/reference_$frames.txt" "$build/candidate_$frames.txt"
    test "$(grep -c '^frame=' "$build/candidate_$frames.txt")" -eq "$frames"
    printf '%s frames: equivalent states and image\n' "$frames"
    grep '^block_admissions=' "$build/candidate_$frames.stats"
done
