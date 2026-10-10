#!/usr/bin/env bash
# 042/043: SM83/device oracle and fixed-line grouping versus the 040 policy.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build/ppu_fixed_lines"
compiler="${CC:-gcc}"
mkdir -p "$build"
common=(-std=gnu11 -O3 -Wall -Wextra -fno-strict-aliasing
    -DPICOGB_CUSTOM=1 -DKNOVGBC_SATURN_CONFIG_TEST=1
    -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1
    -DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1
    -DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_F0_ROM_OPERAND_INLINE=1
    -DKNOVGBC_IDLE_SPIN=1 -DKNOVGBC_IDLE_EVENT=1 -DKNOVGBC_IDLE_RESUME=1
    -DKNOVGBC_IDLE_COUNTDOWN=1 -DKNOVGBC_HRAM_WAIT=1
    -DKNOVGBC_PPU_EVENT_COALESCE=1 -DKNOVGBC_PPU_TWO_LINES=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB")
sources=("$repo/tools/tests/picogb_ppu_fixed_lines_contract.c"
    "$repo/src/saturn/picogb_hram_wait.c"
    "$repo/src/saturn/picogb_idle_spin.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c")

for version in base040 fixed4 fixed8; do
    variant=()
    if [[ "$version" == fixed4 ]]; then
        variant=(-DKNOVGBC_PPU_FIXED_LINES=4)
    elif [[ "$version" == fixed8 ]]; then
        variant=(-DKNOVGBC_PPU_FIXED_LINES=8)
    fi
    "$compiler" "${common[@]}" -flto -flto-partition=one "${variant[@]}" \
        "${sources[@]}" -o "$build/contract_$version" \
        2> "$build/contract_$version.compile.log"
    "$build/contract_$version" > "$build/contract_$version.txt"
    cat "$build/contract_$version.txt"
done

metric() {
    grep -oE "$2=[0-9]+" "$1" | head -n 1 | cut -d= -f2
}
base="$build/contract_base040.txt"
four="$build/contract_fixed4.txt"
eight="$build/contract_fixed8.txt"
for candidate in "$four" "$eight"; do
    for field in edge143 edge_stat edge_sched edge_irq; do
        if [[ "$(metric "$base" "$field")" != \
              "$(metric "$candidate" "$field")" ]]; then
            echo "Fixed-line grouping changed guarded $field versus 040" >&2
            exit 1
        fi
    done
done
for field in edge136 edge139; do
    if [[ "$(metric "$four" "$field")" != \
          "$(metric "$eight" "$field")" ]]; then
        echo "Eight-line fallback changed $field versus four lines" >&2
        exit 1
    fi
done
for field in edge140 edge141 edge142; do
    if [[ "$(metric "$base" "$field")" != \
          "$(metric "$four" "$field")" ||
          "$(metric "$base" "$field")" != \
          "$(metric "$eight" "$field")" ]]; then
        echo "Fixed-line fallback changed $field versus 040" >&2
        exit 1
    fi
done
if (( $(metric "$four" edge0) >= $(metric "$base" edge0) ||
      $(metric "$four" edge135) > $(metric "$base" edge135) ||
      $(metric "$four" edge139) > $(metric "$base" edge139) ||
      $(metric "$eight" edge0) >= $(metric "$four" edge0) ||
      $(metric "$eight" edge135) > $(metric "$four" edge135) ||
      $(metric "$four" groups_total) >= $(metric "$base" groups_total) ||
      $(metric "$eight" groups_total) >= $(metric "$four" groups_total) )); then
    echo 'Fixed-line batching did not reduce groups at its tested boundaries' >&2
    exit 1
fi

for lines in 4 8; do
    "$compiler" "${common[@]}" -DKNOVGBC_PPU_FIXED_LINES="$lines" \
        -g -fsanitize=address,undefined -fno-sanitize-recover=all \
        -fno-omit-frame-pointer "${sources[@]}" \
        -o "$build/contract_fixed${lines}_sanitize" \
        2> "$build/contract_fixed${lines}_sanitize.compile.log"
    ASAN_OPTIONS=detect_leaks=0 "$build/contract_fixed${lines}_sanitize" \
        > "$build/contract_fixed${lines}_sanitize.txt"
    cat "$build/contract_fixed${lines}_sanitize.txt"
done
echo 'ppu_fixed_lines_contract: PASS (040/042/043 edges and sanitized oracle)'
