#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build/idle_stat"
compiler="${CC:-gcc}"
mkdir -p "$build"
flags=(-std=gnu11 -O3 -Wall -Wextra -fno-strict-aliasing
    -DPICOGB_CUSTOM=1 -DKNOVGBC_SATURN_CONFIG_TEST=1
    -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1
    -DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1
    -DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_F0_ROM_OPERAND_INLINE=1
    -DKNOVGBC_IDLE_SPIN=1 -DKNOVGBC_IDLE_EVENT=1 -DKNOVGBC_IDLE_RESUME=1
    -DKNOVGBC_IDLE_COUNTDOWN=1 -DKNOVGBC_IDLE_STAT=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB")
suffix=""
if [[ "${SANITIZE:-0}" == 1 ]]; then
    flags+=(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
    suffix="_sanitize"
else
    flags+=(-flto -flto-partition=one)
fi
sources=("$repo/src/saturn/picogb_idle_spin.c"
    "$repo/src/saturn/picogb_idle_countdown.c"
    "$repo/src/saturn/picogb_idle_stat.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c")
for target in idle_stat:contract idle_resume:regression_resume; do
    source="${target%%:*}"
    binary="$build/${target##*:}$suffix"
    if ! "$compiler" "${flags[@]}" \
        "$repo/tools/tests/picogb_${source}_contract.c" \
        "${sources[@]}" -o "$binary" > "$binary.compile.log" 2>&1; then
        cat "$binary.compile.log"
        exit 1
    fi
    "$binary" | tee "$binary.result.txt"
done
