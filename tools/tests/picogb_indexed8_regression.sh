#!/usr/bin/env bash
# Compare finished DMG pixels after translating indexed output through the
# fixed Saturn RGB1555 CRAM shades. The test includes a mid-frame palette write.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compiler="${CC:-gcc}"
work="$(mktemp -d "$repo/tools/tests/.picogb-indexed8.XXXXXX")"
trap 'rm -f "$work/rgb1555.exe" "$work/indexed8.exe" "$work/rgb1555.txt" "$work/indexed8.txt"; rmdir "$work"' EXIT

common=(
    -std=gnu11 -O3 -Wall -Wextra
    -DPICOGB_CUSTOM=1
    -DKNOVGBC_FAST_MEMORY=1
    -DKNOVGBC_PPU_PALETTE_CACHE=1
    -I"$repo/src/saturn"
    -I"$repo/third_party/PicoGB"
    "$repo/tools/tests/picogb_indexed8_regression.c"
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

"$compiler" "${common[@]}" "${sanitizer_flags[@]}" -o "$work/rgb1555.exe"
"$compiler" "${common[@]}" "${sanitizer_flags[@]}" \
    -DKNOVGBC_VDP2_INDEXED8=1 -o "$work/indexed8.exe"

"$work/rgb1555.exe" > "$work/rgb1555.txt"
"$work/indexed8.exe" > "$work/indexed8.txt"
diff -u "$work/rgb1555.txt" "$work/indexed8.txt"
cat "$work/indexed8.txt"
echo 'picogb_indexed8_regression: PASS'
