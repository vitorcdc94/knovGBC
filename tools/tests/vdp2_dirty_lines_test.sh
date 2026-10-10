#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
mkdir -p "$build"

sources=(
    "$repo/tools/tests/vdp2_dirty_lines_test.c"
    "$repo/src/saturn/platform_video.c"
)
common=(
    -std=gnu11 -Wall -Wextra -Werror
    -DKNOVGBC_VDP2_DIRTY_LINES=1
    -I"$repo/tools/tests/vdp2_mock"
    -I"$repo/src/saturn"
)

"$compiler" "${common[@]}" -O3 -flto "${sources[@]}" \
    -o "$build/vdp2_dirty_lines_test"
"$build/vdp2_dirty_lines_test"

"$compiler" "${common[@]}" -O1 -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "${sources[@]}" -o "$build/vdp2_dirty_lines_test_sanitized"
"$build/vdp2_dirty_lines_test_sanitized"
