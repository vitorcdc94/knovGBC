#!/usr/bin/env bash
# Measure how often two-byte stack accesses stay wholly inside fixed WRAM,
# and compare the 047 baseline with the direct-pair candidate frame by frame.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
build="$repo/build/profile_047_stack_pair"
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

if [[ "${KNOVGBC_STACK_BENCH_ONLY:-0}" == 1 ]]; then
    # No profiling or frame hashing in either executable: this is a paired
    # host timing sanity check, not a substitute for Saturn FPS measurement.
    bench="$build/bench"
    mkdir -p "$bench"
    if [[ ! -x /usr/bin/time ]]; then
        echo 'This bench requires /usr/bin/time.' >&2
        exit 1
    fi
    gameplay=(-DKNOVGBC_SMOKE_LONG=1 -DKNOVGBC_SMOKE_GAMEPLAY_INPUT=1)
    "$compiler" "${common[@]}" "${gameplay[@]}" \
        -o "$bench/reference" 2> "$bench/reference.compile.log"
    "$compiler" "${common[@]}" "${gameplay[@]}" \
        -DKNOVGBC_STACK_PAIR_WRAM=1 \
        -o "$bench/candidate" 2> "$bench/candidate.compile.log"

    for pair in 1 2 3 4 5; do
        if (( pair % 2 )); then
            order=(reference candidate)
        else
            order=(candidate reference)
        fi
        for version in "${order[@]}"; do
            /usr/bin/time -f '%e' -o "$bench/${version}_${pair}.time" \
                "$bench/$version" "$rom" \
                > "$bench/${version}_${pair}.txt" \
                2> "$bench/${version}_${pair}.stats"
        done
        diff -u "$bench/reference_${pair}.txt" \
            "$bench/candidate_${pair}.txt"
        printf 'pair=%s reference_s=%s candidate_s=%s\n' "$pair" \
            "$(< "$bench/reference_${pair}.time")" \
            "$(< "$bench/candidate_${pair}.time")"
    done
    exit 0
fi

for frames in 600 1800 3600; do
    extra=()
    if [[ "$frames" == 1800 ]]; then
        extra=(-DKNOVGBC_SMOKE_LONG=1)
    elif [[ "$frames" == 3600 ]]; then
        extra=(-DKNOVGBC_SMOKE_LONG=1 -DKNOVGBC_SMOKE_GAMEPLAY_INPUT=1)
    fi

    "$compiler" "${common[@]}" "${extra[@]}" \
        -DKNOVGBC_FRAME_TRACE=1 -DKNOVGBC_STACK_PROFILE=1 \
        -o "$build/reference_$frames" \
        2> "$build/reference_$frames.compile.log"
    "$compiler" "${common[@]}" "${extra[@]}" \
        -DKNOVGBC_FRAME_TRACE=1 -DKNOVGBC_STACK_PROFILE=1 \
        -DKNOVGBC_STACK_PAIR_WRAM=1 \
        -o "$build/candidate_$frames" \
        2> "$build/candidate_$frames.compile.log"

    "$build/reference_$frames" "$rom" > "$build/reference_$frames.txt" \
        2> "$build/reference_$frames.stats"
    "$build/candidate_$frames" "$rom" > "$build/candidate_$frames.txt" \
        2> "$build/candidate_$frames.stats"

    # Includes every frame's instruction/cycle count, state hash, memory hash,
    # and image hash, not just the final screen.
    diff -u "$build/reference_$frames.txt" "$build/candidate_$frames.txt"
    diff -u <(grep '^stack_' "$build/reference_$frames.stats") \
        <(grep '^stack_' "$build/candidate_$frames.stats")
    test "$(grep -c '^frame=' "$build/candidate_$frames.txt")" -eq "$frames"
    printf '%s frames: equivalent states and image\n' "$frames"
    grep '^stack_' "$build/reference_$frames.stats"
done
