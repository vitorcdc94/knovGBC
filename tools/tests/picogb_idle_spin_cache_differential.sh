#!/usr/bin/env bash
# Compare recognition caching with the already validated kmvpi029 shortcut.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build/idle_spin_cache"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
mkdir -p "$build"

common=(
    -std=gnu11 -O3 -Wall -Wextra
    -flto -flto-partition=one -fno-strict-aliasing
    -DPICOGB_CUSTOM=1
    -DKNOVGBC_SATURN_CONFIG_TEST=1
    -DKNOVGBC_FAST_MEMORY=1
    -DKNOVGBC_PPU_PALETTE_CACHE=1
    -DKNOVGBC_OPCODE_FETCH_INLINE=1
    -DKNOVGBC_LDH_HRAM_DIRECT=1
    -DKNOVGBC_A7_DIRECT=1
    -DKNOVGBC_F0_ROM_OPERAND_INLINE=1
    -DKNOVGBC_IDLE_SPIN=1
    -I"$repo/src/saturn"
    -I"$repo/third_party/PicoGB"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
    "$repo/src/saturn/picogb_idle_spin.c"
)
cache=(-DKNOVGBC_IDLE_SPIN_CACHE=1 -DKNOVGBC_IDLE_SPIN_CACHE_STATS=1)

"$compiler" "${common[@]}" "${cache[@]}" \
    "$repo/tools/tests/picogb_idle_spin_contract.c" -o "$build/contract"
"$build/contract"

for count in 600 1800; do
    extra=()
    if [[ "$count" == 1800 ]]; then
        extra=(-DKNOVGBC_SMOKE_LONG=1)
    fi
    for variant in reference candidate; do
        selected=()
        if [[ "$variant" == candidate ]]; then selected=("${cache[@]}"); fi
        "$compiler" "${common[@]}" "${extra[@]}" "${selected[@]}" \
            "$repo/tools/tests/picogb_smoke.c" \
            -o "$build/smoke_${count}_${variant}"
        "$build/smoke_${count}_${variant}" "$rom" \
            > "$build/smoke_${count}_${variant}.txt" \
            2> "$build/smoke_${count}_${variant}.stats"
        awk '/^idle_spin_skipped_trips=/' \
            "$build/smoke_${count}_${variant}.stats" \
            > "$build/smoke_${count}_${variant}.trips"
    done
    diff -u "$build/smoke_${count}_reference.txt" \
        "$build/smoke_${count}_candidate.txt"
    diff -u "$build/smoke_${count}_reference.trips" \
        "$build/smoke_${count}_candidate.trips"
    cat "$build/smoke_${count}_candidate.txt"
    cat "$build/smoke_${count}_candidate.stats"
done
printf 'idle_spin_cache_differential: PASS\n'
