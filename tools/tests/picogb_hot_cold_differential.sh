#!/usr/bin/env bash
# Validate the hot/cold layout candidate against the exact kmvpa018 decoder.
# It checks both the extracted opcodes exhaustively and a 600-frame Tetris
# smoke run.  The candidate's only semantic differences are layout attributes
# and complete cold handlers; every observable state hash must match.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
mkdir -p "$build"

common=(
    -std=gnu11 -O3 -Wall -Wextra
    -DNDEBUG
    -DPICOGB_CUSTOM=1
    -DKNOVGBC_SATURN_CONFIG_TEST=1
    -DKNOVGBC_FAST_MEMORY=1
    -DKNOVGBC_PPU_PALETTE_CACHE=1
    -DKNOVGBC_OPCODE_FETCH_INLINE=1
    -DKNOVGBC_LDH_HRAM_DIRECT=1
    -DKNOVGBC_A7_DIRECT=1
    -I"$repo/src/saturn"
    -I"$repo/third_party/PicoGB"
)

core=(
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)

candidate=( -DKNOVGBC_CB_COLD=1 -DKNOVGBC_HOT_COLD_SPLIT=1 )

"$compiler" "${common[@]}" "$repo/tools/tests/picogb_hot_cold_contract.c" \
    "${core[@]}" -o "$build/picogb_hot_cold_reference"
"$compiler" "${common[@]}" "${candidate[@]}" \
    "$repo/tools/tests/picogb_hot_cold_contract.c" "${core[@]}" \
    -o "$build/picogb_hot_cold_candidate"

"$build/picogb_hot_cold_reference" > "$build/picogb_hot_cold_reference.txt"
"$build/picogb_hot_cold_candidate" > "$build/picogb_hot_cold_candidate.txt"
diff -u "$build/picogb_hot_cold_reference.txt" \
    "$build/picogb_hot_cold_candidate.txt"

"$compiler" "${common[@]}" "$repo/tools/tests/picogb_smoke.c" \
    "${core[@]}" -o "$build/picogb_hot_cold_smoke_reference"
"$compiler" "${common[@]}" "${candidate[@]}" \
    "$repo/tools/tests/picogb_smoke.c" "${core[@]}" \
    -o "$build/picogb_hot_cold_smoke_candidate"

"$build/picogb_hot_cold_smoke_reference" "$rom" \
    > "$build/picogb_hot_cold_smoke_reference.txt"
"$build/picogb_hot_cold_smoke_candidate" "$rom" \
    > "$build/picogb_hot_cold_smoke_candidate.txt"
diff -u "$build/picogb_hot_cold_smoke_reference.txt" \
    "$build/picogb_hot_cold_smoke_candidate.txt"

cat "$build/picogb_hot_cold_candidate.txt"
cat "$build/picogb_hot_cold_smoke_candidate.txt"
