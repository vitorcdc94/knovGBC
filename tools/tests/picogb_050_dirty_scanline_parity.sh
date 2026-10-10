#!/usr/bin/env bash
# Keep kmvpe048's PPU_MODE=8 generic renderer as the reference. Compare the
# isolated dirty-scanline candidate after each synthetic visual dependency
# change and over 3,600 real Tetris frames with gameplay input.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
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
    -DKNOVGBC_STACK_PAIR_WRAM=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB"
    "$repo/src/saturn/picogb_idle_spin.c"
    "$repo/src/saturn/picogb_idle_countdown.c"
    "$repo/src/saturn/picogb_hram_wait.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)

synthetic_sanitizer=()
if [[ "${SANITIZE_SYNTHETIC:-0}" == 1 ]]; then
    synthetic_sanitizer=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi

for version in reference dirty; do
    variant=()
    if [[ "$version" == dirty ]]; then
        variant=(-DKNOVGBC_PPU_DIRTY_SCANLINES=1)
    fi

    "$compiler" "${common[@]}" "${variant[@]}" \
        "${synthetic_sanitizer[@]}" \
        -DKNOVGBC_PPU_DIRTY_REGRESSION=1 \
        "$repo/tools/tests/picogb_ppu_regression.c" \
        -o "$build/dirty_scanline_${version}_synthetic" \
        2> "$build/dirty_scanline_${version}_synthetic_compile.log"
    "$build/dirty_scanline_${version}_synthetic" \
        > "$build/dirty_scanline_${version}_synthetic.txt"

    if [[ "${SYNTHETIC_ONLY:-0}" == 1 ]]; then
        continue
    fi

    "$compiler" "${common[@]}" "${variant[@]}" \
        -DKNOVGBC_SMOKE_LONG=1 -DKNOVGBC_SMOKE_GAMEPLAY_INPUT=1 \
        -DKNOVGBC_FRAME_TRACE=1 \
        "$repo/tools/tests/picogb_smoke.c" \
        -o "$build/dirty_scanline_${version}_tetris" \
        2> "$build/dirty_scanline_${version}_tetris_compile.log"
    "$build/dirty_scanline_${version}_tetris" "$rom" \
        > "$build/dirty_scanline_${version}_tetris.txt"
done

diff -u "$build/dirty_scanline_reference_synthetic.txt" \
    "$build/dirty_scanline_dirty_synthetic.txt"
if [[ "${SYNTHETIC_ONLY:-0}" == 1 ]]; then
    echo 'dirty scanline mode8: synthetic dependencies match kmvpe048'
    exit 0
fi
diff -u "$build/dirty_scanline_reference_tetris.txt" \
    "$build/dirty_scanline_dirty_tetris.txt"
test "$(grep -c '^frame=' "$build/dirty_scanline_dirty_tetris.txt")" -eq 3600
echo 'dirty scanline mode8: synthetic dependencies and 3600 Tetris frames match kmvpe048'
