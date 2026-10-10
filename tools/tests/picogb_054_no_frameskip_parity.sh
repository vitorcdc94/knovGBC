#!/usr/bin/env bash
# Compare the kmvpz053 core with the isolated full-visual-rate configuration.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
rom="${1:-$repo/roms/tetris.gb}"
mkdir -p "$build"

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
    -DKNOVGBC_SMOKE_GAMEPLAY_INPUT=1 -DKNOVGBC_FRAME_TRACE=1
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

for variant in baseline fullrate; do
    extra=()
    if [[ "$variant" == fullrate ]]; then
        extra=(-DKNOVGBC_NO_FRAMESKIP=1)
    fi
    "$compiler" "${common[@]}" "${extra[@]}" \
        "$repo/tools/tests/picogb_smoke.c" \
        -o "$build/visual_rate_${variant}" \
        2> "$build/visual_rate_${variant}_compile.log"
    "$build/visual_rate_${variant}" "$rom" \
        > "$build/visual_rate_${variant}.txt"
    test "$(grep -c '^frame=' "$build/visual_rate_${variant}.txt")" -eq 3600
    sed -n '/^frame=/s/ image=[^ ]*//p' \
        "$build/visual_rate_${variant}.txt" \
        > "$build/visual_rate_${variant}_state.txt"
done

# Rendering policy must not change CPU instructions, cycles, memory or IRQs.
diff -u "$build/visual_rate_baseline_state.txt" \
    "$build/visual_rate_fullrate_state.txt"

# Every framebuffer update visible in the old half-rate build must still
# appear at the same logical frame in the full-rate build.
awk '
    /^frame=/ {
        frame = $1
        image = ""
        for (i = 2; i <= NF; i++) {
            if ($i ~ /^image=/) image = $i
        }
        if (FNR == NR) {
            if (image != previous) {
                visible[frame] = image
                baseline_changes++
            }
            previous = image
        } else {
            if ((frame in visible) && visible[frame] != image) {
                print "framebuffer mismatch at " frame > "/dev/stderr"
                exit 1
            }
            if (image != previous_full) fullrate_changes++
            previous_full = image
        }
    }
    END {
        if (baseline_changes == 0 || fullrate_changes <= baseline_changes) {
            print "full-rate build did not add visual updates" > "/dev/stderr"
            exit 1
        }
        printf "3600 logical frames match; framebuffer updates %u -> %u\n", \
            baseline_changes, fullrate_changes
    }
' "$build/visual_rate_baseline.txt" "$build/visual_rate_fullrate.txt"
