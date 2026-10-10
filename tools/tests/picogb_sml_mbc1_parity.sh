#!/usr/bin/env bash
# Host-only Super Mario Land MBC1 smoke/parity test for the 050 PPU change.
# The ROM is supplied by the user and is never copied into the test output.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/Super Mario Land (World) (Rev 1).gb}"
mkdir -p "$build"

if [[ ! -f "$rom" || "$(stat -c %s "$rom")" != 65536 ]]; then
    echo 'Expected the unmodified 64 KiB Super Mario Land ROM' >&2
    exit 1
fi

common=(
    -std=gnu11 -O3 -Wall -Wextra -flto -flto-partition=one
    -fno-strict-aliasing -DPICOGB_CUSTOM=1
    -DKNOVGBC_SATURN_CONFIG_TEST=1 -DKNOVGBC_FAST_MEMORY=1
    -DKNOVGBC_PPU_PALETTE_CACHE=1 -DKNOVGBC_OPCODE_FETCH_INLINE=1
    -DKNOVGBC_LDH_HRAM_DIRECT=1 -DKNOVGBC_A7_DIRECT=1
    -DKNOVGBC_F0_ROM_OPERAND_INLINE=1 -DKNOVGBC_IDLE_SPIN=1
    -DKNOVGBC_IDLE_EVENT=1 -DKNOVGBC_IDLE_RESUME=1
    -DKNOVGBC_IDLE_COUNTDOWN=1 -DKNOVGBC_HRAM_WAIT=1
    -DKNOVGBC_PPU_EVENT_COALESCE=1 -DKNOVGBC_PPU_TWO_LINES=1
    -DKNOVGBC_PPU_ADAPTIVE_BATCH=1 -DKNOVGBC_VDP2_DMA_DIRECT=1
    -DKNOVGBC_SMPC_NONBLOCKING=1 -DKNOVGBC_VDP2_INDEXED8=1
    -DKNOVGBC_STACK_PAIR_WRAM=1 -DKNOVGBC_SMOKE_MBC1=1
    -DKNOVGBC_FRAME_TRACE=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB"
    "$repo/src/saturn/picogb_idle_spin.c"
    "$repo/src/saturn/picogb_idle_countdown.c"
    "$repo/src/saturn/picogb_hram_wait.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c"
)

for version in reference dirty; do
    variant=()
    run_args=("$rom")
    if [[ "$version" == dirty ]]; then
        variant=(-DKNOVGBC_PPU_DIRTY_SCANLINES=1)
        if [[ "${CAPTURE_PGM:-0}" == 1 ]]; then
            variant+=(-DKNOVGBC_SMOKE_PGM_CAPTURE=1)
            run_args+=("$build")
        fi
    fi

    "$compiler" "${common[@]}" "${variant[@]}" \
        "$repo/tools/tests/picogb_smoke.c" \
        -o "$build/sml_mbc1_${version}" \
        2> "$build/sml_mbc1_${version}.compile.log"
    "$build/sml_mbc1_${version}" "${run_args[@]}" \
        > "$build/sml_mbc1_${version}.txt"
    test "$(grep -c '^frame=' "$build/sml_mbc1_${version}.txt")" -eq 3600
done

diff -u "$build/sml_mbc1_reference.txt" "$build/sml_mbc1_dirty.txt"
echo 'SML MBC1: banks 2/3 data+fetch+cache passed; 3600 frames match the non-dirty PPU reference'
