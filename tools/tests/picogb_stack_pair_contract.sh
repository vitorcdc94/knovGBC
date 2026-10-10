#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/build/stack_pair_contract"
compiler="${CC:-gcc}"
mkdir -p "$build"

common=(
    -std=gnu11 -O3 -Wall -Wextra -flto -flto-partition=one
    -fno-strict-aliasing -DPICOGB_CUSTOM=1
    -DKNOVGBC_SATURN_CONFIG_TEST=1 -DKNOVGBC_FAST_MEMORY=1
    -DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1
    -DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_F0_ROM_OPERAND_INLINE=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB"
    "$repo/tools/tests/picogb_stack_pair_contract.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)

"$compiler" "${common[@]}" -o "$build/reference" \
    2> "$build/reference.compile.log"
"$compiler" "${common[@]}" -DKNOVGBC_STACK_PAIR_WRAM=1 \
    -o "$build/candidate" 2> "$build/candidate.compile.log"

"$build/reference" > "$build/reference.txt"
"$build/candidate" > "$build/candidate.txt"
diff -u "$build/reference.txt" "$build/candidate.txt"
test "$(grep -c '^case=' "$build/candidate.txt")" -eq 45
printf '45 stack-boundary cases: equivalent state and memory\n'
