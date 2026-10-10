#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
mkdir -p "$build"
common=(-std=gnu11 -Wall -Wextra -Werror -DPICOGB_CUSTOM=1
    -I"$repo/tools/tests/audio056_apu_include" -I"$repo/third_party/PicoGB")
sources=("$repo/third_party/PicoGB/apu.c" "$repo/tools/tests/audio056_apu_test.c")
"$compiler" "${common[@]}" -O3 "${sources[@]}" -o "$build/audio056_apu_test"
"$build/audio056_apu_test"
"$compiler" "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    "${sources[@]}" -o "$build/audio056_apu_test_sanitized"
"$build/audio056_apu_test_sanitized"
