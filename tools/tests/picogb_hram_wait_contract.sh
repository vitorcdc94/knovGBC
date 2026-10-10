#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build/hram_wait"
compiler="${CC:-gcc}"
mkdir -p "$build"
flags=(-std=gnu11 -O3 -Wall -Wextra -fno-strict-aliasing
    -DPICOGB_CUSTOM=1 -DKNOVGBC_SATURN_CONFIG_TEST=1
    -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1
    -DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1
    -DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_F0_ROM_OPERAND_INLINE=1
    -DKNOVGBC_IDLE_SPIN=1 -DKNOVGBC_IDLE_EVENT=1 -DKNOVGBC_IDLE_RESUME=1
    -DKNOVGBC_IDLE_COUNTDOWN=1 -DKNOVGBC_HRAM_WAIT=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB")
if [[ "${SANITIZE:-0}" == 1 ]]; then
    flags+=(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
    binary="$build/contract_sanitize"
else
    flags+=(-flto -flto-partition=one)
    binary="$build/contract"
fi
"$compiler" "${flags[@]}" \
    "$repo/tools/tests/picogb_hram_wait_contract.c" \
    "$repo/src/saturn/picogb_hram_wait.c" \
    "$repo/src/saturn/picogb_idle_spin.c" \
    "$repo/third_party/PicoGB/mi.c" \
    "$repo/third_party/PicoGB/microcode_dispatch.c" \
    "$repo/third_party/PicoGB/microcode.c" \
    "$repo/third_party/PicoGB/ppu.c" \
    "$repo/third_party/PicoGB/fabric.c" -o "$binary"
"$binary"
