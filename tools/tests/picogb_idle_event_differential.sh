#!/usr/bin/env bash
# Compare the first-event-boundary shortcut with kmvpi029 and no shortcut.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build/idle_event"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
mkdir -p "$build"
common=(
    -std=gnu11 -O3 -Wall -Wextra -flto -flto-partition=one -fno-strict-aliasing
    -DPICOGB_CUSTOM=1 -DKNOVGBC_SATURN_CONFIG_TEST=1
    -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1
    -DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1
    -DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_F0_ROM_OPERAND_INLINE=1
    -DKNOVGBC_FRAME_TRACE=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)
for frames in 600 1800 3600; do
    extra=()
    if [[ "$frames" == 1800 ]]; then extra=(-DKNOVGBC_SMOKE_LONG=1); fi
    if [[ "$frames" == 3600 ]]; then
        extra=(-DKNOVGBC_SMOKE_LONG=1 -DKNOVGBC_SMOKE_GAMEPLAY_INPUT=1)
    fi
    for version in plain baseline event; do
        variant=()
        if [[ "$version" != plain ]]; then
            variant=(-DKNOVGBC_IDLE_SPIN=1 "$repo/src/saturn/picogb_idle_spin.c")
        fi
        if [[ "$version" == event ]]; then variant+=(-DKNOVGBC_IDLE_EVENT=1); fi
        "$compiler" "${common[@]}" "${extra[@]}" "${variant[@]}" \
            "$repo/tools/tests/picogb_smoke.c" -o "$build/${version}_${frames}" \
            2> "$build/${version}_${frames}.compile.log"
        "$build/${version}_${frames}" "$rom" > "$build/${version}_${frames}.txt" \
            2> "$build/${version}_${frames}.stats"
    done
    diff -u "$build/plain_${frames}.txt" "$build/baseline_${frames}.txt"
    diff -u "$build/baseline_${frames}.txt" "$build/event_${frames}.txt"
    tail -n 3 "$build/event_${frames}.txt"
    cat "$build/baseline_${frames}.stats" "$build/event_${frames}.stats"
done
echo 'idle_event_differential: PASS (each frame + final state, cycles and framebuffer)'
