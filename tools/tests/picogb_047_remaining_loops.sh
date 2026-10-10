#!/usr/bin/env bash
# Profile only the interpreter calls left by the exact kmvpe047 CPU/PPU flags.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
build="$repo/build/profile_047_remaining_loops"
mkdir -p "$build"

"$compiler" -std=gnu11 -O3 -Wall -Wextra -flto -flto-partition=one \
    -fno-strict-aliasing -DPICOGB_CUSTOM=1 \
    -DKNOVGBC_SATURN_CONFIG_TEST=1 -DKNOVGBC_FAST_MEMORY=1 \
    -DKNOVGBC_PPU_PALETTE_CACHE=1 -DKNOVGBC_OPCODE_FETCH_INLINE=1 \
    -DKNOVGBC_LDH_HRAM_DIRECT=1 -DKNOVGBC_A7_DIRECT=1 \
    -DKNOVGBC_F0_ROM_OPERAND_INLINE=1 -DKNOVGBC_IDLE_SPIN=1 \
    -DKNOVGBC_IDLE_EVENT=1 -DKNOVGBC_IDLE_RESUME=1 \
    -DKNOVGBC_IDLE_COUNTDOWN=1 -DKNOVGBC_HRAM_WAIT=1 \
    -DKNOVGBC_PPU_EVENT_COALESCE=1 -DKNOVGBC_PPU_TWO_LINES=1 \
    -DKNOVGBC_PPU_ADAPTIVE_BATCH=1 -DKNOVGBC_VDP2_DMA_DIRECT=1 \
    -DKNOVGBC_SMPC_NONBLOCKING=1 -DKNOVGBC_VDP2_INDEXED8=1 \
    -DKNOVGBC_SMOKE_LONG=1 -DKNOVGBC_SMOKE_GAMEPLAY_INPUT=1 \
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB" \
    "$repo/tools/tests/picogb_remaining_loops.c" \
    "$repo/src/saturn/picogb_idle_spin.c" \
    "$repo/src/saturn/picogb_idle_countdown.c" \
    "$repo/src/saturn/picogb_hram_wait.c" \
    "$repo/third_party/PicoGB/mi.c" \
    "$repo/third_party/PicoGB/microcode_dispatch.c" \
    "$repo/third_party/PicoGB/microcode.c" \
    "$repo/third_party/PicoGB/ppu.c" \
    "$repo/third_party/PicoGB/fabric.c" \
    -o "$build/profile"

"$build/profile" "$rom" > "$build/profile.txt" 2> "$build/profile.stats"
cat "$build/profile.txt" "$build/profile.stats"
