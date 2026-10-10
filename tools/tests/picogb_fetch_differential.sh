#!/usr/bin/env bash
# Compare the baseline opcode fetch with the inline-ROM prefetch experiment
# under the exact Master+VDP2 PicoGB configuration. Both runs must produce
# identical deterministic smoke output before the Saturn image is built.
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

common=(
    -std=gnu11 -O3 -Wall -Wextra
    -flto -flto-partition=one -fno-strict-aliasing
    "${sanitize[@]}"
    -DPICOGB_CUSTOM=1
    -DKNOVGBC_SATURN_CONFIG_TEST=1
    -DKNOVGBC_FAST_MEMORY=1
    -DKNOVGBC_PPU_PALETTE_CACHE=1
    -I"$repo/src/saturn"
    -I"$repo/third_party/PicoGB"
    "$repo/tools/tests/picogb_smoke.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)

"$compiler" "${common[@]}" -o "$build/picogb_fetch_reference"
"$compiler" "${common[@]}" -DKNOVGBC_OPCODE_FETCH_INLINE=1 \
    -o "$build/picogb_fetch_inline"

"$build/picogb_fetch_reference" "$rom" > "$build/picogb_fetch_reference.txt"
"$build/picogb_fetch_inline" "$rom" > "$build/picogb_fetch_inline.txt"
diff -u "$build/picogb_fetch_reference.txt" "$build/picogb_fetch_inline.txt"
cat "$build/picogb_fetch_inline.txt"

contract=(
    -std=gnu11 -O3 -Wall -Wextra
    -flto -flto-partition=one -fno-strict-aliasing
    "${sanitize[@]}"
    -DPICOGB_CUSTOM=1
    -DKNOVGBC_SATURN_CONFIG_TEST=1
    -DKNOVGBC_FAST_MEMORY=1
    -DKNOVGBC_PPU_PALETTE_CACHE=1
    -I"$repo/src/saturn"
    -I"$repo/third_party/PicoGB"
    "$repo/tools/tests/picogb_fetch_contract.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)

"$compiler" "${contract[@]}" -o "$build/picogb_fetch_contract_reference"
"$compiler" "${contract[@]}" -DKNOVGBC_OPCODE_FETCH_INLINE=1 \
    -o "$build/picogb_fetch_contract_inline"

"$build/picogb_fetch_contract_reference" > "$build/picogb_fetch_contract_reference.txt"
"$build/picogb_fetch_contract_inline" > "$build/picogb_fetch_contract_inline.txt"
diff -u "$build/picogb_fetch_contract_reference.txt" \
    "$build/picogb_fetch_contract_inline.txt"
cat "$build/picogb_fetch_contract_inline.txt"
