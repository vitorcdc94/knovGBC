#!/usr/bin/env bash
# Build the open SameBoy boot ROMs with a verified, project-local RGBDS.
set -Eeuo pipefail

project_root=/mnt/knovGBC
archive="$project_root/third_party/rgbds-linux-x86_64-v1.0.3.tar.xz"
rgbds_root="$project_root/third_party/rgbds-1.0.3"
bootrom_output="$project_root/src/saturn/bootroms"
expected_sha256=280a52061a0c516999bee75ac357628d6d50784309e0486cef25f7460e6f330b

if [[ ! -f "$archive" ]]; then
    echo "Missing verified RGBDS archive: $archive" >&2
    exit 1
fi

actual_sha256=$(sha256sum "$archive" | awk '{print $1}')
if [[ "$actual_sha256" != "$expected_sha256" ]]; then
    echo "RGBDS archive SHA-256 mismatch: $actual_sha256" >&2
    exit 1
fi

# Reject path traversal before extracting the trusted release archive.
if tar -tf "$archive" | grep -Eq '(^/|(^|/)\.\.(/|$))'; then
    echo "Unsafe path found in RGBDS archive" >&2
    exit 1
fi

mkdir -p "$rgbds_root"
tar -xJf "$archive" --no-same-owner -C "$rgbds_root"

rgbasm=$(find "$rgbds_root" -type f -name rgbasm -print -quit)
if [[ -z "$rgbasm" ]]; then
    echo "rgbasm was not found after extracting RGBDS" >&2
    exit 1
fi
rgbds_bin=$(dirname "$rgbasm")
chmod u+x "$rgbds_bin"/rgbasm "$rgbds_bin"/rgblink "$rgbds_bin"/rgbgfx

make -B -C "$project_root/third_party/SameBoy" \
    RGBDS="$rgbds_bin/" \
    build/bin/BootROMs/dmg_boot.bin \
    build/bin/BootROMs/cgb_boot_fast.bin

dmg="$project_root/third_party/SameBoy/build/bin/BootROMs/dmg_boot.bin"
cgb="$project_root/third_party/SameBoy/build/bin/BootROMs/cgb_boot_fast.bin"

[[ $(stat -c '%s' "$dmg") == 256 ]]
[[ $(stat -c '%s' "$cgb") == 2304 ]]

mkdir -p "$bootrom_output"
install -m 0644 "$dmg" "$bootrom_output/dmg_boot.bin"
install -m 0644 "$cgb" "$bootrom_output/cgb_boot_fast.bin"

sha256sum "$bootrom_output/dmg_boot.bin" \
    "$bootrom_output/cgb_boot_fast.bin"
