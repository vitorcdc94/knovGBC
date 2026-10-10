#!/usr/bin/env bash
# Compare the kmvpa018 C interpreter with the isolated F0 ROM operand fetch.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
if [[ "${SMOKE_LONG:-0}" == "1" ]]; then
    build="$build/f0_operand_long"
fi
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
    -I"$repo/src/saturn"
    -I"$repo/third_party/PicoGB"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)
if [[ "${SMOKE_LONG:-0}" == "1" ]]; then
    common+=(-DKNOVGBC_SMOKE_LONG=1)
fi

for test in picogb_smoke picogb_sm83_contract picogb_f0_rom_operand_contract; do
    "$compiler" "${common[@]}" "$repo/tools/tests/$test.c" \
        -o "$build/${test}_f0_operand_reference"
    "$compiler" "${common[@]}" -DKNOVGBC_F0_ROM_OPERAND_INLINE=1 \
        "$repo/tools/tests/$test.c" \
        -o "$build/${test}_f0_operand_candidate"

    if [[ "$test" == picogb_smoke ]]; then
        "$build/${test}_f0_operand_reference" "$rom" \
            > "$build/${test}_f0_operand_reference.txt"
        "$build/${test}_f0_operand_candidate" "$rom" \
            > "$build/${test}_f0_operand_candidate.txt"
    else
        "$build/${test}_f0_operand_reference" \
            > "$build/${test}_f0_operand_reference.txt"
        "$build/${test}_f0_operand_candidate" \
            > "$build/${test}_f0_operand_candidate.txt"
    fi

    diff -u "$build/${test}_f0_operand_reference.txt" \
        "$build/${test}_f0_operand_candidate.txt"
    cat "$build/${test}_f0_operand_candidate.txt"
done
