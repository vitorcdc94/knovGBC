#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
mkdir -p "$build"

# The zoom is an isolated build; verify that the unzoomed presenter still
# obeys its previous DMA/HUD contract before testing the new layout.
/bin/bash "$repo/tools/tests/vdp2_dma_indexed8_test.sh"

source="$repo/tools/tests/vdp2_dma_indexed8_test.c"
common=(
    -std=gnu11 -Wall -Wextra -Werror
    -fno-pie -no-pie
    -DKNOVGBC_VDP2_INDEXED8=1
    -DKNOVGBC_VDP2_DMA_DIRECT=1
    -DKNOVGBC_VDP2_ZOOM=1
    -I"$repo/tools/tests/vdp2_indexed8_mock"
    -I"$repo/src/saturn"
)

for variant in zoom zoom3 zoom60; do
    extra=()
    if [[ "$variant" == zoom3 || "$variant" == zoom60 ]]; then
        extra=(-DKNOVGBC_HUD_3_DIGITS=1)
    fi
    if [[ "$variant" == zoom60 ]]; then
        extra+=(-DKNOVGBC_NO_FRAMESKIP=1)
    fi

    "$compiler" "${common[@]}" "${extra[@]}" -O3 -flto "$source" \
        -o "$build/vdp2_${variant}_test"
    "$build/vdp2_${variant}_test"

    "$compiler" "${common[@]}" "${extra[@]}" -O1 -g \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -fno-omit-frame-pointer \
        "$source" -o "$build/vdp2_${variant}_test_sanitized"
    "$build/vdp2_${variant}_test_sanitized"
done
