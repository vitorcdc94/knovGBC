#!/usr/bin/env bash
# Run the existing exhaustive register/ALU contract against both the PicoGB
# decoder and the compact-C front end.  The smoke differential covers whole
# Tetris execution; this catches flag/cycle errors in the direct opcode paths.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
mkdir -p "$build"

common=(
    -std=gnu11 -O2 -Wall -Wextra
    -DPICOGB_CUSTOM=1
    -I"$repo/tools/tests/picogb_include"
    -I"$repo/src/saturn"
    -I"$repo/third_party/PicoGB"
    "$repo/tools/tests/picogb_sm83_contract.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)

"$compiler" "${common[@]}" -o "$build/picogb_sm83_contract_reference"
"$compiler" "${common[@]}" \
    -DKNOVGBC_SM83_COMPACT=1 \
    "$repo/src/saturn/picogb_sm83_compact.c" \
    -o "$build/picogb_sm83_contract_compact"

"$build/picogb_sm83_contract_reference"
"$build/picogb_sm83_contract_compact"
