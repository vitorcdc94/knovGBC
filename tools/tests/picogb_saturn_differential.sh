#!/usr/bin/env bash
# Compare the reference and complete DMG PPU paths under the exact Saturn
# PicoGB configuration.  Both executables run the same Tetris smoke workload;
# their complete textual results (including framebuffer and machine hashes)
# must be identical.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
mkdir -p "$build"

common=(
    -std=gnu11 -O3 -Wall -Wextra
    -DPICOGB_CUSTOM=1
    -DKNOVGBC_SATURN_CONFIG_TEST=1
    -DKNOVGBC_FAST_MEMORY=1
    -DKNOVGBC_PPU_PALETTE_CACHE=1
    -I"$repo/src/saturn"
    -I"$repo/third_party/PicoGB"
    "$repo/tools/tests/picogb_smoke.c"
    "$repo/src/saturn/picogb_ppu_tile_copy.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)

"$compiler" "${common[@]}" -o "$build/picogb_saturn_reference"
"$compiler" "${common[@]}" -DKNOVGBC_PPU_FULL_OPT=1 \
    -o "$build/picogb_saturn_ppu_full"

"$build/picogb_saturn_reference" "$rom" > "$build/picogb_saturn_reference.txt"
"$build/picogb_saturn_ppu_full" "$rom" > "$build/picogb_saturn_ppu_full.txt"
diff -u "$build/picogb_saturn_reference.txt" "$build/picogb_saturn_ppu_full.txt"
cat "$build/picogb_saturn_ppu_full.txt"
