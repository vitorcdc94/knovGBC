#!/usr/bin/env bash
# Run actual Tetris gameplay under the 048 core and the 049 capture reference.
# Compare every logical frame's machine/image hash and reconstruct every
# native visual frame pixel by pixel before the Saturn hardware test.
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
    -DKNOVGBC_SMOKE_LONG=1 -DKNOVGBC_SMOKE_GAMEPLAY_INPUT=1
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

"$compiler" "${common[@]}" -o "$build/kmvpe048_tetris_host" \
    2> "$build/kmvpe048_tetris_compile.log"
"$compiler" "${common[@]}" \
    -DKNOVGBC_VDP12_NATIVE=1 -DKNOVGBC_VDP12_SOFTWARE_REFERENCE=1 \
    -DKNOVGBC_VDP12_SMOKE_CHECK=1 \
    "$repo/src/saturn/picogb_vdp12_frame.c" \
    -o "$build/kmvpe049_tetris_host" \
    2> "$build/kmvpe049_tetris_compile.log"

"$build/kmvpe048_tetris_host" "$rom" > "$build/kmvpe048_tetris.txt" \
    2> "$build/kmvpe048_tetris.stats"
"$build/kmvpe049_tetris_host" "$rom" > "$build/kmvpe049_tetris.txt" \
    2> "$build/kmvpe049_tetris.stats"

diff -u "$build/kmvpe048_tetris.txt" "$build/kmvpe049_tetris.txt"
test "$(grep -c '^frame=' "$build/kmvpe049_tetris.txt")" -eq 3600
grep '^vdp12_visual_frames=' "$build/kmvpe049_tetris.stats"
echo 'Tetris 048/049: 3600 logical frames identical'
