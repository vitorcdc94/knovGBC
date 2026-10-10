#!/usr/bin/env bash
# Run a knovGBC make target through the space-free WSL project bind mount.
set -Eeuo pipefail

project_root=/mnt/knovGBC

export YAUL_INSTALL_ROOT="$project_root/toolchain"
export YAUL_PROG_SH_PREFIX=sh2eb-elf
export YAUL_ARCH_SH_PREFIX=sh2eb-elf
export YAUL_ARCH_M68K_PREFIX=m68keb-elf
export YAUL_OPTION_MALLOC_IMPL=tlsf
export MAKE_ISO_XORRISOFS=/usr/bin/xorrisofs
export SILENT=1
export NOCOLOR=1

exec make -C "$project_root" "$@"
