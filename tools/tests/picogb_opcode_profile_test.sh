#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
mkdir -p "$build"

sanitize=()
if [[ "${SANITIZE:-0}" == "1" ]]; then
    sanitize=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi

"$compiler" -std=gnu11 -O3 -Wall -Wextra -Werror "${sanitize[@]}" \
    -DKNOVGBC_OPCODE_PROFILE=1 -I"$repo/src/saturn" \
    "$repo/src/saturn/picogb_opcode_profile.c" \
    "$repo/tools/tests/picogb_opcode_profile_test.c" \
    -o "$build/picogb_opcode_profile_test"
"$build/picogb_opcode_profile_test"
