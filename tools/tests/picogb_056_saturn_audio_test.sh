#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$repo/tools/tests/picogb_build/audio056_backend"
compiler="${CC:-gcc}"
mkdir -p "$build"
common=(-std=gnu11 -Wall -Wextra -Werror
    -I"$repo/tools/tests/audio056_mock" -I"$repo/src/saturn")
source="$repo/tools/tests/picogb_056_saturn_audio_test.c"
"$compiler" "${common[@]}" -O3 -flto "$source" -o "$build/backend_test"
"$build/backend_test"
"$compiler" "${common[@]}" -O1 -g -fno-pie -no-pie \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$source" -o "$build/backend_test_sanitized"
"$build/backend_test_sanitized"
