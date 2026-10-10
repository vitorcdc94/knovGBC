#!/usr/bin/env bash
# Compare full-rate 054 with the isolated 055 tile-pattern invalidation.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build/tile_dirty_055"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
mkdir -p "$build"

if ! grep -q 'KNOVGBC_PPU_TILE_DIRTY_LINES' "$repo/third_party/PicoGB/ppu.c"; then
    echo 'The 055 tile-pattern invalidation is not present in ppu.c' >&2
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
    -DKNOVGBC_STACK_PAIR_WRAM=1 -DKNOVGBC_PPU_DIRTY_SCANLINES=1
    -DKNOVGBC_NO_FRAMESKIP=1
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

for version in baseline candidate; do
    variant=()
    if [[ "$version" == candidate ]]; then
        variant=(-DKNOVGBC_PPU_TILE_DIRTY_LINES=1)
    fi

    "$compiler" "${common[@]}" "${variant[@]}" \
        "$repo/tools/tests/picogb_055_tile_dirty_synthetic.c" \
        -o "$build/${version}_synthetic" \
        2> "$build/${version}_synthetic.compile.log"
    "$build/${version}_synthetic" > "$build/${version}_synthetic.txt"

    "$compiler" "${common[@]}" "${variant[@]}" -O1 -g -fno-lto \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -fno-omit-frame-pointer -fno-pie -no-pie \
        "$repo/tools/tests/picogb_055_tile_dirty_synthetic.c" \
        -o "$build/${version}_synthetic_sanitized" \
        2> "$build/${version}_synthetic_sanitized.compile.log"
    "$build/${version}_synthetic_sanitized" \
        > "$build/${version}_synthetic_sanitized.txt"
    diff -u "$build/${version}_synthetic.txt" \
        "$build/${version}_synthetic_sanitized.txt"

    if [[ "${SYNTHETIC_ONLY:-0}" == 1 ]]; then
        continue
    fi

    "$compiler" "${common[@]}" "${variant[@]}" \
        -DKNOVGBC_SMOKE_GAMEPLAY_INPUT=1 -DKNOVGBC_FRAME_TRACE=1 \
        "$repo/tools/tests/picogb_smoke.c" \
        -o "$build/${version}_gameplay" \
        2> "$build/${version}_gameplay.compile.log"
    "$build/${version}_gameplay" "$rom" > "$build/${version}_gameplay.txt"
    test "$(grep -c '^frame=' "$build/${version}_gameplay.txt")" -eq 3600
done

diff -u "$build/baseline_synthetic.txt" "$build/candidate_synthetic.txt"
if [[ "${SYNTHETIC_ONLY:-0}" == 1 ]]; then
    echo '055 synthetic indexed-PPU cases match 054 at full visual rate'
    exit 0
fi

# Every logical frame includes instructions, cycles, state, memory and image.
diff -u "$build/baseline_gameplay.txt" "$build/candidate_gameplay.txt"
echo '055: synthetic cases and all 3600 gameplay frame/state/image traces match 054'
