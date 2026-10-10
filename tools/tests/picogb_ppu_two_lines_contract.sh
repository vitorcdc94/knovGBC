#!/usr/bin/env bash
# 040: independent per-instruction/device oracle, plus 039/040 boundary probes.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build/ppu_two_lines"
compiler="${CC:-gcc}"
mkdir -p "$build"
common=(-std=gnu11 -O3 -Wall -Wextra -fno-strict-aliasing
    -DPICOGB_CUSTOM=1 -DKNOVGBC_SATURN_CONFIG_TEST=1
    -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1
    -DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1
    -DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_F0_ROM_OPERAND_INLINE=1
    -DKNOVGBC_IDLE_SPIN=1 -DKNOVGBC_IDLE_EVENT=1 -DKNOVGBC_IDLE_RESUME=1
    -DKNOVGBC_IDLE_COUNTDOWN=1 -DKNOVGBC_HRAM_WAIT=1
    -DKNOVGBC_PPU_EVENT_COALESCE=1
    -I"$repo/src/saturn" -I"$repo/third_party/PicoGB")
sources=("$repo/tools/tests/picogb_hram_wait_contract.c"
    "$repo/src/saturn/picogb_hram_wait.c"
    "$repo/src/saturn/picogb_idle_spin.c"
    "$repo/third_party/PicoGB/mi.c"
    "$repo/third_party/PicoGB/microcode_dispatch.c"
    "$repo/third_party/PicoGB/microcode.c"
    "$repo/third_party/PicoGB/ppu.c"
    "$repo/third_party/PicoGB/fabric.c")

for version in base039 candidate040; do
    extra=()
    if [[ "$version" == candidate040 ]]; then
        extra=(-DKNOVGBC_PPU_TWO_LINES=1)
    fi
    "$compiler" "${common[@]}" -flto -flto-partition=one "${extra[@]}" \
        "${sources[@]}" -o "$build/contract_$version" \
        2> "$build/contract_$version.compile.log"
    "$build/contract_$version" > "$build/contract_$version.txt"
    cat "$build/contract_$version.txt"
done

metric() {
    grep -oE "$2=[0-9]+" "$1" | head -n 1 | cut -d= -f2
}
base="$build/contract_base039.txt"
candidate="$build/contract_candidate040.txt"
for field in edge142 edge143 edge_stat edge_sched; do
    if [[ "$(metric "$base" "$field")" != "$(metric "$candidate" "$field")" ]]; then
        echo "040 changed guarded $field behavior versus 039" >&2
        exit 1
    fi
done
if (( $(metric "$candidate" edge141) >= $(metric "$base" edge141) )); then
    echo 'Two-line path was not exercised at visible line 141' >&2
    exit 1
fi
if (( $(metric "$candidate" candidate_groups_total) >= \
      $(metric "$base" candidate_groups_total) )); then
    echo '040 did not reduce candidate group count across the contract' >&2
    exit 1
fi

"$compiler" "${common[@]}" -DKNOVGBC_PPU_TWO_LINES=1 \
    -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    -fno-omit-frame-pointer "${sources[@]}" \
    -o "$build/contract_candidate040_sanitize" \
    2> "$build/contract_candidate040_sanitize.compile.log"
ASAN_OPTIONS=detect_leaks=0 "$build/contract_candidate040_sanitize" \
    > "$build/contract_candidate040_sanitize.txt"
cat "$build/contract_candidate040_sanitize.txt"
echo 'ppu_two_lines_contract: PASS (039 vs 040 edges and sanitized oracle)'
