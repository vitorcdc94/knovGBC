#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
mkdir -p "$build"
common=(-std=gnu11 -Wall -Wextra -Werror -DPICOGB_CUSTOM=1
    -I"$repo/tools/tests/audio057_apu_include" -I"$repo/third_party/PicoGB")
renames=()
for name in apu_t apu_ch_t apu_initialize apu_reset apu_write apu_read \
    apu_write_wave apu_read_wave apu_div_reset apu_flush apu_render \
    apu_render_faster apu_tick_internal_internals apu_tick_internal apu_tick; do
    renames+=("-D$name=audio056_$name")
done

for mode in mono_fast mono_fallback stereo_fallback; do
    flags=(-DKNOVGBC_APU_MONO_FAST=1)
    if [[ "$mode" == mono_fallback ]]; then
        flags=(-DKNOVGBC_APU_MONO_FAST=0)
    elif [[ "$mode" == stereo_fallback ]]; then
        flags+=(-DAUDIO057_TEST_STEREO=1)
    fi
    for validation in optimized sanitized; do
        optimization=(-O3 -flto)
        if [[ "$validation" == sanitized ]]; then
            optimization=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
        fi
        prefix="$build/audio057_apu_${mode}_${validation}"
        "$compiler" "${common[@]}" "${flags[@]}" "${optimization[@]}" \
            "${renames[@]}" -c "$repo/tools/tests/fixtures/audio056/apu.c" -o "$prefix.reference.o"
        "$compiler" "${common[@]}" "${flags[@]}" "${optimization[@]}" \
            "$repo/third_party/PicoGB/apu.c" "$repo/tools/tests/audio057_apu_test.c" \
            "$prefix.reference.o" -o "$prefix"
        "$prefix"
    done
done
