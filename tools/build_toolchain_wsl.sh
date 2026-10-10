#!/usr/bin/env bash
# Build the SH-2 compiler in-place. Run from the WSL bind mount /mnt/knovGBC.
set -Eeuo pipefail

project_root=/mnt/knovGBC
ctng_dir="$project_root/third_party/crosstool-ng-1.28.0"
config="$project_root/third_party/libyaul-build-scripts/configs/sh2eb-elf/native-linux.config"
status_file="$project_root/.toolchain-build.status"

finish() {
    result=$?
    if [ "$result" -eq 0 ]; then
        printf 'success\n' > "$status_file"
    else
        printf 'failed (%s)\n' "$result" > "$status_file"
    fi
    exit "$result"
}
trap finish EXIT
trap 'exit 128' HUP INT TERM

cd "$ctng_dir"
cp "$config" .config
./ct-ng olddefconfig
sed -i 's#^CT_PREFIX_DIR=.*#CT_PREFIX_DIR="/mnt/knovGBC/toolchain"#' .config
sed -i 's#^CT_LOCAL_TARBALLS_DIR=.*#CT_LOCAL_TARBALLS_DIR="/mnt/knovGBC/.toolchain-src"#' .config
# GMP 6.2.1 predates GCC 15's C23-default behaviour; retain the GNU C17
# semantics expected by its configure-time compiler probes.
sed -i 's#^CT_GMP_EXTRA_CFLAGS=.*#CT_GMP_EXTRA_CFLAGS="-std=gnu17"#' .config
./ct-ng build
