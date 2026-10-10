#!/usr/bin/env bash
# Source-handoff checks using only synthetic inputs; no SDK or cartridge ROM.
set -uo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
mkdir -p "$repo/build/publication-tests"
failed=0
for test in audio057_apu_test picogb_056_saturn_audio_test vdp2_zoom_test \
    picogb_sm83_contract picogb_ppu_regression picogb_055_tile_dirty_differential; do
    log="$repo/build/publication-tests/$test.log"
    if SYNTHETIC_ONLY=1 bash "$repo/tools/tests/$test.sh" >"$log" 2>&1; then
        printf 'PASS %s\n' "$test"
        tail -n 2 "$log"
    else
        printf 'FAIL %s\n' "$test" >&2
        tail -n 12 "$log" >&2
        failed=1
    fi
done
exit "$failed"
