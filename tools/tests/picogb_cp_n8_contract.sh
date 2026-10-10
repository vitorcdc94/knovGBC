#!/usr/bin/env bash
# Exhaustive CP n8 contract on the exact 036 CPU flags/configuration.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build/cp_n8"
compiler="${CC:-gcc}"
mkdir -p "$build"
common=(
    -std=gnu11 -O3 -Wall -Wextra -flto -flto-partition=one -fno-strict-aliasing
    -DPICOGB_CUSTOM=1 -DKNOVGBC_SATURN_CONFIG_TEST=1
    -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1
    -DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1
    -DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_F0_ROM_OPERAND_INLINE=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
    "$repo/tools/tests/picogb_cp_n8_contract.c"
)
"$compiler" "${common[@]}" -o "$build/reference"
"$compiler" "${common[@]}" -DKNOVGBC_CP_N8_DIRECT=1 -o "$build/candidate"
"$build/reference" > "$build/reference.txt"
"$build/candidate" > "$build/candidate.txt"
diff -u "$build/reference.txt" "$build/candidate.txt"
cat "$build/candidate.txt"

"$compiler" "${common[@]}" -DKNOVGBC_CP_N8_DIRECT=1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -o "$build/candidate_sanitize"
"$build/candidate_sanitize" > "$build/candidate_sanitize.txt"
diff -u "$build/candidate.txt" "$build/candidate_sanitize.txt"
echo 'picogb_cp_n8_contract: PASS (O3/LTO and ASan/UBSan)'
