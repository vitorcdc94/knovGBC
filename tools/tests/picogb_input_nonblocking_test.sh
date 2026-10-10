#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build"
compiler="${CC:-gcc}"
mkdir -p "$build"

common=(
    -std=gnu11 -Wall -Wextra -Werror
    -DKNOVGBC_SMPC_NONBLOCKING=1
    -I"$repo/tools/tests/mock_smpc"
    -I"$repo/src/saturn"
)
sources=(
    "$repo/src/saturn/picogb_mvp_input.c"
    "$repo/tools/tests/picogb_input_nonblocking_test.c"
)

"$compiler" "${common[@]}" -O3 -flto "${sources[@]}" \
    -o "$build/picogb_input_nonblocking_test"
"$build/picogb_input_nonblocking_test"

"$compiler" "${common[@]}" -O1 -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "${sources[@]}" -o "$build/picogb_input_nonblocking_test_sanitized"
"$build/picogb_input_nonblocking_test_sanitized"
