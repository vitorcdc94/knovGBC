#!/usr/bin/env bash
# Compare kmvpa018 against the isolated JR Z,e8 fast path.  The full Tetris
# smoke run and the explicit control-flow contract must remain identical.
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
    -o "$build/picogb_jr_z_reference"
"$compiler" "${common[@]}" -DKNOVGBC_JR_Z_DIRECT=1 \
    "$repo/tools/tests/picogb_smoke.c" -o "$build/picogb_jr_z_direct"

"$build/picogb_jr_z_reference" "$rom" > "$build/picogb_jr_z_reference.txt"
"$build/picogb_jr_z_direct" "$rom" > "$build/picogb_jr_z_direct.txt"
diff -u "$build/picogb_jr_z_reference.txt" "$build/picogb_jr_z_direct.txt"
cat "$build/picogb_jr_z_direct.txt"

"$compiler" "${common[@]}" "$repo/tools/tests/picogb_sm83_contract.c" \
    -o "$build/picogb_jr_z_contract_reference"
"$compiler" "${common[@]}" -DKNOVGBC_JR_Z_DIRECT=1 \
    "$repo/tools/tests/picogb_sm83_contract.c" -o "$build/picogb_jr_z_contract_direct"

"$build/picogb_jr_z_contract_reference" > "$build/picogb_jr_z_contract_reference.txt"
"$build/picogb_jr_z_contract_direct" > "$build/picogb_jr_z_contract_direct.txt"
diff -u "$build/picogb_jr_z_contract_reference.txt" \
    "$build/picogb_jr_z_contract_direct.txt"
cat "$build/picogb_jr_z_contract_direct.txt"
