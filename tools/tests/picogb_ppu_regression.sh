#!/usr/bin/env bash
# Build the synthetic PPU regression against the reference and complete
# renderer paths. The two outputs must match exactly; the optimized build also
# checks its no-change dirty-frame elision internally.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
mkdir -p "$build"

common=(
    -std=gnu11 -O3 -Wall -Wextra
    -DPICOGB_CUSTOM=1
    -DKNOVGBC_FAST_MEMORY=1
    -DKNOVGBC_PPU_PALETTE_CACHE=1
    -I"$repo/src/saturn"
    -I"$repo/third_party/PicoGB"
    "$repo/tools/tests/picogb_ppu_regression.c"
    "$repo/src/saturn/picogb_ppu_tile_copy.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)

sanitizer_flags=()
if [[ "${SANITIZE:-0}" == "1" ]]; then
    sanitizer_flags=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi

"$compiler" "${common[@]}" "${sanitizer_flags[@]}" \
    -o "$build/picogb_ppu_reference"
"$compiler" "${common[@]}" "${sanitizer_flags[@]}" -DKNOVGBC_PPU_FULL_OPT=1 \
    -o "$build/picogb_ppu_full"

"$build/picogb_ppu_reference" > "$build/picogb_ppu_reference.txt"
"$build/picogb_ppu_full" > "$build/picogb_ppu_full.txt"
diff -u "$build/picogb_ppu_reference.txt" "$build/picogb_ppu_full.txt"
cat "$build/picogb_ppu_full.txt"
