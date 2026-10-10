#!/usr/bin/env bash
# Isolated 057 mono-mixer parity against the frozen 056 APU.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build/audio_057"
compiler="${CC:-gcc}"
mkdir -p "$build"
bash "$repo/tools/tests/audio057_apu_test.sh"
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
    -DKNOVGBC_STACK_PAIR_WRAM=1 -DKNOVGBC_PPU_DIRTY_SCANLINES=1
    -DKNOVGBC_PPU_TILE_DIRTY_LINES=1 -DKNOVGBC_NO_FRAMESKIP=1
    -DKNOVGBC_SMOKE_GAMEPLAY_INPUT=1 -DKNOVGBC_SMOKE_LONG=1
    -DKNOVGBC_FRAME_TRACE=1 -DKNOVGBC_AUDIO=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB"
    "$repo/src/saturn/picogb_idle_spin.c"
    "$repo/src/saturn/picogb_idle_countdown.c"
    "$repo/src/saturn/picogb_hram_wait.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
    "$repo/tools/tests/picogb_smoke.c"
)
for version in reference preserved mono sanitized; do
    variant=(-DKNOVGBC_APU_MONO_FAST=0 "$repo/third_party/PicoGB/apu.c")
    if [[ "$version" == reference ]]; then
        variant=(-DKNOVGBC_APU_MONO_FAST=0 "$repo/tools/tests/fixtures/audio056/apu.c")
    elif [[ "$version" != preserved ]]; then
        variant=(-DKNOVGBC_APU_MONO_FAST=1 "$repo/third_party/PicoGB/apu.c")
    fi
    if [[ "$version" == sanitized ]]; then
        variant+=(-O1 -g -fno-lto -fsanitize=address,undefined
            -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie)
    fi
    "$compiler" "${common[@]}" "${variant[@]}" -o "$build/$version" \
        2> "$build/$version.compile.log"
    "$build/$version" "$repo/roms/tetris.gb" > "$build/$version.txt" \
        2> "$build/$version.audio.txt"
    test "$(grep -c '^frame=' "$build/$version.txt")" -eq 3600
done
for version in preserved mono sanitized; do
    diff -u "$build/reference.txt" "$build/$version.txt"
    diff -u "$build/reference.audio.txt" "$build/$version.audio.txt"
done
cat "$build/mono.audio.txt"
echo '057: all 3600 CPU/state/image traces and PCM checksum match frozen 056'
