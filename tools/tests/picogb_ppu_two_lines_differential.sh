#!/usr/bin/env bash
# Compare the Saturn-equivalent 039 core with only the optional 040 change.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build/ppu_two_lines"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
mkdir -p "$build"
common=(-std=gnu11 -O3 -Wall -Wextra -flto -flto-partition=one
    -fno-strict-aliasing -DPICOGB_CUSTOM=1
    -DKNOVGBC_SATURN_CONFIG_TEST=1 -DKNOVGBC_FAST_MEMORY=1
    -DKNOVGBC_PPU_PALETTE_CACHE=1 -DKNOVGBC_OPCODE_FETCH_INLINE=1
    -DKNOVGBC_LDH_HRAM_DIRECT=1 -DKNOVGBC_A7_DIRECT=1
    -DKNOVGBC_F0_ROM_OPERAND_INLINE=1 -DKNOVGBC_IDLE_SPIN=1
    -DKNOVGBC_IDLE_EVENT=1 -DKNOVGBC_IDLE_RESUME=1
    -DKNOVGBC_IDLE_COUNTDOWN=1 -DKNOVGBC_HRAM_WAIT=1
    -DKNOVGBC_PPU_EVENT_COALESCE=1 -DKNOVGBC_FRAME_TRACE=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB"
    "$repo/src/saturn/picogb_idle_spin.c"
    "$repo/src/saturn/picogb_idle_countdown.c"
    "$repo/src/saturn/picogb_hram_wait.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c")
for frames in 600 1800 3600; do
    extra=()
    if [[ "$frames" == 1800 ]]; then extra=(-DKNOVGBC_SMOKE_LONG=1); fi
    if [[ "$frames" == 3600 ]]; then
        extra=(-DKNOVGBC_SMOKE_LONG=1 -DKNOVGBC_SMOKE_GAMEPLAY_INPUT=1)
    fi
    for version in base039 candidate040; do
        variant=()
        if [[ "$version" == candidate040 ]]; then
            variant=(-DKNOVGBC_PPU_TWO_LINES=1)
        fi
        "$compiler" "${common[@]}" "${extra[@]}" "${variant[@]}" \
            "$repo/tools/tests/picogb_smoke.c" -o "$build/${version}_${frames}" \
            2> "$build/${version}_${frames}.compile.log"
        "$build/${version}_${frames}" "$rom" \
            > "$build/${version}_${frames}.txt" \
            2> "$build/${version}_${frames}.stats"
    done
    diff -u "$build/base039_${frames}.txt" \
        "$build/candidate040_${frames}.txt"
    base_groups="$(sed -n 's/.*hram_wait_calls=[0-9]* groups=\([0-9]*\).*/\1/p' \
        "$build/base039_${frames}.stats")"
    candidate_groups="$(sed -n 's/.*hram_wait_calls=[0-9]* groups=\([0-9]*\).*/\1/p' \
        "$build/candidate040_${frames}.stats")"
    if [[ -z "$base_groups" || -z "$candidate_groups" ]] || \
        (( candidate_groups >= base_groups )); then
        echo "040 did not reduce HRAM wait groups in $frames frames" >&2
        exit 1
    fi
    printf '%s frames: same frame/cycle/state/memory/image trace; groups %s -> %s\n' \
        "$frames" "$base_groups" "$candidate_groups"
done
echo 'ppu_two_lines_differential: PASS'
