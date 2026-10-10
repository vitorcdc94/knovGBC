#!/usr/bin/env bash
# Exercise the exact per-line visual replay hooks used by kmvps012. This is a
# host-only protocol test; Saturn hardware still verifies dual-SH-2 startup,
# P2 coherence and throughput.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
mkdir -p "$build"

sanitizer_flags=()
if [[ "${SANITIZE:-0}" == "1" ]]; then
    sanitizer_flags=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi

"$compiler" -std=gnu11 -O3 -Wall -Wextra \
    -DPICOGB_CUSTOM=1 \
    -DKNOVGBC_FAST_MEMORY=1 \
    -DKNOVGBC_PPU_PALETTE_CACHE=1 \
    -DKNOVGBC_SLAVE_PPU_SHADOW=1 \
    -I"$repo/src/saturn" \
    -I"$repo/third_party/PicoGB" \
    "$repo/tools/tests/picogb_shadow_trace_regression.c" \
    "$repo/third_party/PicoGB/mi.c" \
    "$repo/third_party/PicoGB/microcode_dispatch.c" \
    "$repo/third_party/PicoGB/microcode.c" \
    "$repo/third_party/PicoGB/ppu.c" \
    "$repo/third_party/PicoGB/fabric.c" \
    "${sanitizer_flags[@]}" \
    -o "$build/picogb_shadow_trace_regression"

"$build/picogb_shadow_trace_regression"
