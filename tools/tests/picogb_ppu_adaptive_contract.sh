#!/usr/bin/env bash
# 041: instruction/device oracle, 040/041 grouping comparison and sanitizer.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build/ppu_adaptive"
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
sources=("$repo/tools/tests/picogb_ppu_adaptive_contract.c"
    "$repo/src/saturn/picogb_hram_wait.c"
    "$repo/src/saturn/picogb_idle_spin.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c")

for version in base040 candidate041; do
    variant=()
    if [[ "$version" == candidate041 ]]; then
        variant=(-DKNOVGBC_PPU_ADAPTIVE_BATCH=1)
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
candidate="$build/contract_candidate041.txt"
for field in edge_sched edge_irq; do
    if [[ "$(metric "$base" "$field")" != \
          "$(metric "$candidate" "$field")" ]]; then
        echo "041 changed guarded $field grouping versus 040" >&2
        exit 1
    fi
done
if (( $(metric "$candidate" edge143) > $(metric "$base" edge143) )); then
    echo '041 regressed grouping at the VBlank boundary' >&2
    exit 1
fi
if (( $(metric "$candidate" edge_stat) > $(metric "$base" edge_stat) )); then
    echo '041 regressed grouping at the enabled STAT edge' >&2
    exit 1
fi
if (( $(metric "$candidate" edge0) >= $(metric "$base" edge0) )); then
    echo 'Adaptive path was not exercised at visible line 0' >&2
    exit 1
fi
if (( $(metric "$candidate" groups_total) >= \
      $(metric "$base" groups_total) )); then
    echo '041 did not reduce groups across the adaptive contract' >&2
    exit 1
fi

"$compiler" "${common[@]}" -DKNOVGBC_PPU_ADAPTIVE_BATCH=1 \
    -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    -fno-omit-frame-pointer "${sources[@]}" \
    -o "$build/contract_candidate041_sanitize" \
    2> "$build/contract_candidate041_sanitize.compile.log"
ASAN_OPTIONS=detect_leaks=0 "$build/contract_candidate041_sanitize" \
    > "$build/contract_candidate041_sanitize.txt"
cat "$build/contract_candidate041_sanitize.txt"
echo 'ppu_adaptive_contract: PASS (040 vs 041 edges and sanitized oracle)'
