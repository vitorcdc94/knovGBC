#!/usr/bin/env bash
# Validate the portable-C basic-block executor against the exact kmvpa018
# host configuration. This deliberately does not compile the old SH-2 JIT or
# define KNOVGBC_BLOCK_RECOMPILER: the candidate has only the new compact C
# executor and mb_exec() remains its compatibility fallback.
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
    -std=gnu11 -O3 -DNDEBUG -Wall -Wextra
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

build_pair() {
    local suffix="$1"
    shift

    "$compiler" "${common[@]}" "$@" "$repo/tools/tests/picogb_smoke.c" \
        -o "$build/picogb_block_static_reference_$suffix"
    "$compiler" "${common[@]}" -DKNOVGBC_BLOCK_STATIC=1 "$@" \
        "$repo/src/saturn/picogb_block_executor.c" \
        "$repo/tools/tests/picogb_smoke.c" \
        -o "$build/picogb_block_static_candidate_$suffix"

    "$build/picogb_block_static_reference_$suffix" "$rom" > \
        "$build/picogb_block_static_reference_$suffix.txt"
    "$build/picogb_block_static_candidate_$suffix" "$rom" > \
        "$build/picogb_block_static_candidate_$suffix.txt"
    diff -u "$build/picogb_block_static_reference_$suffix.txt" \
        "$build/picogb_block_static_candidate_$suffix.txt"
    cat "$build/picogb_block_static_candidate_$suffix.txt"
}

# Standard 600-frame path, then the longer deterministic menu/gameplay path.
build_pair normal
build_pair long -DKNOVGBC_SMOKE_LONG=1

"$compiler" "${common[@]}" -DKNOVGBC_BLOCK_STATIC=1 \
    "$repo/src/saturn/picogb_block_executor.c" \
    "$repo/tools/tests/picogb_block_static_contract.c" \
    -o "$build/picogb_block_static_contract"
"$build/picogb_block_static_contract"
