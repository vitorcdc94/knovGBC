#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
mkdir -p "$build"

source="$repo/tools/tests/vdp2_dma_direct_test.c"
common=(
    -std=gnu11 -Wall -Wextra -Werror
    -fno-pie -no-pie
    -DKNOVGBC_VDP2_DMA_DIRECT=1
    -I"$repo/tools/tests/vdp2_dma_mock"
    -I"$repo/src/saturn"
)

"$compiler" "${common[@]}" -O3 -flto "$source" \
    -o "$build/vdp2_dma_direct_test"
"$build/vdp2_dma_direct_test"

"$compiler" "${common[@]}" -O1 -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$source" -o "$build/vdp2_dma_direct_test_sanitized"
"$build/vdp2_dma_direct_test_sanitized"
