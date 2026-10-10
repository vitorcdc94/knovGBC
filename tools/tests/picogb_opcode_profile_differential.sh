#!/usr/bin/env bash
# Build the opcode/memory profiler on top of the exact kmvpa018 host
# configuration. The diagnostic must preserve Tetris state and framebuffer;
# only its additional profile_* lines are allowed to differ.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
mkdir -p "$build"

sanitize=()
if [[ "${SANITIZE:-0}" == "1" ]]; then
    sanitize=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi

"$repo/tools/tests/picogb_opcode_profile_test.sh"

common=(
    -std=gnu11 -O3 -Wall -Wextra
    -flto -flto-partition=one -fno-strict-aliasing
    "${sanitize[@]}"
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

"$compiler" "${common[@]}" "$repo/tools/tests/picogb_smoke.c" \
    -o "$build/picogb_opcode_profile_reference"
"$compiler" "${common[@]}" -DKNOVGBC_OPCODE_PROFILE=1 \
    "$repo/src/saturn/picogb_opcode_profile.c" \
    "$repo/tools/tests/picogb_smoke.c" \
    -o "$build/picogb_opcode_profile_candidate"

"$build/picogb_opcode_profile_reference" "$rom" > \
    "$build/picogb_opcode_profile_reference.txt"
"$build/picogb_opcode_profile_candidate" "$rom" > \
    "$build/picogb_opcode_profile_candidate.txt"

sed '/^profile_/d' "$build/picogb_opcode_profile_candidate.txt" > \
    "$build/picogb_opcode_profile_candidate_normal.txt"
diff -u "$build/picogb_opcode_profile_reference.txt" \
    "$build/picogb_opcode_profile_candidate_normal.txt"

grep -Eq '^profile_instructions=[1-9][0-9]* ' \
    "$build/picogb_opcode_profile_candidate.txt"
grep -Eq ' profile_data=[1-9][0-9]* ' \
    "$build/picogb_opcode_profile_candidate.txt"

executed_instructions="$(sed -n \
    's/^endian=.* instructions=\([0-9][0-9]*\) cycles=.*/\1/p' \
    "$build/picogb_opcode_profile_candidate.txt")"
profile_instructions="$(sed -n \
    's/^profile_instructions=\([0-9][0-9]*\) .*/\1/p' \
    "$build/picogb_opcode_profile_candidate.txt")"
profile_interrupts="$(sed -n \
    's/^profile_instructions=[0-9][0-9]* profile_interrupts=\([0-9][0-9]*\) .*/\1/p' \
    "$build/picogb_opcode_profile_candidate.txt")"
[[ "$executed_instructions" -eq \
    "$((profile_instructions + profile_interrupts))" ]]

grep '^profile_' "$build/picogb_opcode_profile_candidate.txt"
