#!/usr/bin/env bash
# Build a standalone Saturn boot probe through the project's WSL mount.
# It intentionally does not invoke Yaul's application CRT or libyaul.
set -Eeuo pipefail

project_root=/mnt/knovGBC
toolchain_root="$project_root/toolchain"
build_dir="$project_root/build/bootprobe"
stage_dir="$project_root/build/bootprobe-cdroot"
dist_dir="$project_root/dist"
program=bootv1

export YAUL_INSTALL_ROOT="$toolchain_root"
export YAUL_ARCH_SH_PREFIX=sh2eb-elf
export YAUL_PROG_SH_PREFIX=sh2eb-elf
export MAKE_ISO_XORRISOFS=/usr/bin/xorrisofs

cc="$toolchain_root/bin/sh2eb-elf-gcc"
objcopy="$toolchain_root/bin/sh2eb-elf-objcopy"
nm="$toolchain_root/bin/sh2eb-elf-nm"
objdump="$toolchain_root/bin/sh2eb-elf-objdump"

mkdir -p "$build_dir" "$stage_dir" "$dist_dir"

"$cc" \
    -m2 -mb \
    -x assembler-with-cpp \
    -ffreestanding -fno-builtin -fno-stack-protector \
    -nostdlib -nostartfiles -nodefaultlibs \
    -Wl,-T,"$project_root/src/saturn/bootprobe.ld" \
    -Wl,-Map,"$build_dir/$program.map" \
    -Wl,--gc-sections -Wl,-e,_start \
    -o "$build_dir/$program.elf" \
    "$project_root/src/saturn/bootprobe.sx"

"$objcopy" -O binary "$build_dir/$program.elf" "$build_dir/$program.bin"
"$objdump" -f "$build_dir/$program.elf"

if "$nm" "$build_dir/$program.elf" | grep -Eq '(___sys_init|___cpu_init|user_early_init| main$)'; then
    echo "error: the boot probe unexpectedly linked application runtime code" >&2
    exit 1
fi

"$toolchain_root/bin/make-ip" \
    "$build_dir/$program.bin" \
    "V1.000" \
    20260812 \
    "JUE" \
    "J" \
    "knovGBC BOOT PROBE V1" \
    0x060FFFF0 \
    0x060FFFF0 \
    0x06004000 \
    -1

cp "$build_dir/$program.bin" "$stage_dir/1ST_READ.BIN"
printf 'empty\n' > "$stage_dir/ABS.TXT"
printf 'empty\n' > "$stage_dir/BIB.TXT"
printf 'empty\n' > "$stage_dir/CPY.TXT"

"$toolchain_root/bin/make-iso" \
    "$stage_dir" "$build_dir/IP.BIN" "$dist_dir" "$program"

cp "$dist_dir/$program.iso" "$dist_dir/$program.bin"
printf 'FILE "%s.bin" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n' \
    "$program" > "$dist_dir/$program.cue"
rm -f "$dist_dir/$program.iso"

echo
echo "Boot probe built: $dist_dir/$program.bin"
echo "SHA-256: $(sha256sum "$dist_dir/$program.bin" | awk '{print $1}')"
