#!/usr/bin/env bash
# Build and install Yaul using the project-local SH-2 toolchain.
# This script runs from the WSL bind mount at /mnt/knovGBC.
set -Eeuo pipefail

project_root=/mnt/knovGBC
yaul_root="$project_root/third_party/libyaul"
status_file="$project_root/.yaul-build.status"

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

export YAUL_INSTALL_ROOT="$project_root/toolchain"
export YAUL_PROG_SH_PREFIX=sh2eb-elf
export YAUL_ARCH_SH_PREFIX=sh2eb-elf
export YAUL_ARCH_M68K_PREFIX=m68keb-elf
export YAUL_BUILD_ROOT="$yaul_root"
export YAUL_BUILD=build
export YAUL_OPTION_MALLOC_IMPL=tlsf
export SILENT=1
export NOCOLOR=1

test -x "$YAUL_INSTALL_ROOT/bin/sh2eb-elf-gcc"
test -d "$yaul_root/libyaul"

make -C "$yaul_root" install-debug
make -C "$yaul_root" install-tools
