#!/usr/bin/env bash
# Compare the original per-opcode device scheduler with the conservative
# event-batched scheduler under the exact Saturn PicoGB configuration.
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
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)

"$compiler" "${common[@]}" -o "$build/picogb_scheduler_reference"
"$compiler" "${common[@]}" -DKNOVGBC_SCHEDULER_BATCH=1 \
    -o "$build/picogb_scheduler_batch"

"$build/picogb_scheduler_reference" "$rom" > "$build/picogb_scheduler_reference.txt"
"$build/picogb_scheduler_batch" "$rom" > "$build/picogb_scheduler_batch.txt"
diff -u "$build/picogb_scheduler_reference.txt" \
    "$build/picogb_scheduler_batch.txt"
cat "$build/picogb_scheduler_batch.txt"
