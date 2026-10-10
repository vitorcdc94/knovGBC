#!/usr/bin/env bash
# Compare the kmvpe046 core with indexed PPU output from kmvpe047. The host
# hashes indexed pixels through the fixed CRAM shades before comparing traces.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
work="$(mktemp -d "$repo/tools/tests/.picogb-indexed8-smoke.XXXXXX")"
trap 'rm -f "$work"/*.exe "$work"/*.txt "$work"/*.log; rmdir "$work"' EXIT

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
    -DKNOVGBC_SMPC_NONBLOCKING=1 -DKNOVGBC_FRAME_TRACE=1
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

    for version in rgb1555 indexed8; do
        variant=()
        if [[ "$version" == indexed8 ]]; then
            variant=(-DKNOVGBC_VDP2_INDEXED8=1)
        fi
        if ! "$compiler" "${common[@]}" "${extra[@]}" "${variant[@]}" \
            -o "$work/${version}_${frames}.exe" \
            2> "$work/${version}_${frames}.log"; then
            cat "$work/${version}_${frames}.log" >&2
            exit 1
        fi
        "$work/${version}_${frames}.exe" "$rom" \
            > "$work/${version}_${frames}.txt"
        count="$(grep -c '^frame=' "$work/${version}_${frames}.txt")"
        if [[ "$count" != "$frames" ]]; then
            echo "$version: expected $frames frame traces, got $count" >&2
            exit 1
        fi
    done

    diff -u "$work/rgb1555_${frames}.txt" \
        "$work/indexed8_${frames}.txt"
    printf '%s frames: identical CPU, machine, memory and visual traces\n' \
        "$frames"
done

echo 'picogb_indexed8_smoke_differential: PASS'
