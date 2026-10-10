#!/usr/bin/env bash
# Portable publication build entry point. Historical build scripts are separate.
set -Eeuo pipefail

usage() {
    cat <<'USAGE'
Usage: bash tools/build.sh [--profile PROFILE] (--rom FILE | --probe) [--elf-only]
       bash tools/build.sh --check [--profile PROFILE] [--rom FILE | --probe] [--elf-only]

Profiles (default: mvpfastzoomaudiomono):
  mvpfastzoomaudiomono  057 configuration, mono mixer and audio
  mvpfastzoomaudio      056 configuration, audio
  mvpfastzoomtile       055 configuration, no audio
  mvpfastsml            051 configuration, 64 KiB DMG MBC1 without RAM

--rom FILE  Embed a compatible user-supplied ROM in isolated build output.
--probe     Generate an original 32 KiB compile/package fixture (not a game).
--elf-only  Link an ELF without making a disc or using Yaul's IP security blobs.
--check     Validate source/input only; list missing build prerequisites without
            compiling, writing files, requiring a ROM, or requiring an SDK.

Use a checkout and YAUL_INSTALL_ROOT without spaces. YAUL_INSTALL_ROOT defaults
to <checkout>/toolchain. This script does not install or download dependencies.
USAGE
}

fail() { printf 'error: %s\n' "$*" >&2; exit 1; }
profile=mvpfastzoomaudiomono
rom=
probe=0
check=0
elf_only=0
while (($#)); do
    case "$1" in
        --profile|--rom)
            (($# >= 2)) || fail "$1 requires a value"
            [[ -n "$2" && "$2" != --* ]] || fail "$1 requires a value"
            if [[ "$1" == --profile ]]; then profile=$2; else rom=$2; fi
            shift 2 ;;
        --probe) probe=1; shift ;;
        --check) check=1; shift ;;
        --elf-only) elf_only=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) fail "unknown option: $1 (use --help)" ;;
    esac
done
[[ -z "$rom" || "$probe" == 0 ]] || fail '--rom and --probe are mutually exclusive'

case "$profile" in
    mvpfastzoomaudiomono) program=knpub057; rom_class=dmg-rom-only ;;
    mvpfastzoomaudio) program=knpub056; rom_class=dmg-rom-only ;;
    mvpfastzoomtile) program=knpub055; rom_class=dmg-rom-only ;;
    mvpfastsml) program=knpub051; rom_class=dmg-mbc1 ;;
    *) fail "unsupported profile: $profile (use --help)" ;;
esac
[[ "$probe" == 0 || "$rom_class" == dmg-rom-only ]] ||
    fail '--probe is a 32 KiB ROM-only fixture; it cannot use the MBC1 profile'

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
[[ "$project_root" != *[[:space:]]* ]] ||
    fail "Yaul requires a checkout without spaces. Copy this checkout into your Linux/WSL home directory, for example ~/knovGBC. Current path: $project_root"
command -v python3 >/dev/null || fail 'python3 is required for source/input validation'

source_files=(Makefile tools/make_build_probe.py src/saturn/picogb_mvp_main.c
    src/saturn/picogb_mvp_input.c src/saturn/pgb_config.h
    src/saturn/boot_entry.sx src/saturn/platform_video.c
    src/saturn/mvp_profiler.c src/saturn/saroo_bios_shim.c
    src/saturn/master_only_cpu_init.c src/saturn/picogb_idle_spin.c
    src/saturn/picogb_idle_countdown.c src/saturn/picogb_hram_wait.c
    third_party/PicoGB/mi.c third_party/PicoGB/microcode.c
    third_party/PicoGB/microcode_dispatch.c third_party/PicoGB/ppu.c
    third_party/PicoGB/fabric.c)
if [[ "$profile" == mvpfastzoomaudio || "$profile" == mvpfastzoomaudiomono ]]; then
    source_files+=(third_party/PicoGB/apu.c src/saturn/picogb_saturn_audio.c)
fi
for source in "${source_files[@]}"; do
    [[ -f "$project_root/$source" ]] || fail "source file missing: $source"
done

input_args=(--class "$rom_class")
if [[ -n "$rom" ]]; then input_args+=(--rom "$rom"); fi
if [[ "$probe" == 1 ]]; then input_args+=(--probe); fi
python3 "$project_root/tools/make_build_probe.py" "${input_args[@]}" --check

export YAUL_INSTALL_ROOT="${YAUL_INSTALL_ROOT:-$project_root/toolchain}"
[[ "$YAUL_INSTALL_ROOT" = /* ]] || YAUL_INSTALL_ROOT="$PWD/$YAUL_INSTALL_ROOT"
[[ "$YAUL_INSTALL_ROOT" != *[[:space:]]* ]] ||
    fail 'YAUL_INSTALL_ROOT must not contain spaces'
export YAUL_PROG_SH_PREFIX="${YAUL_PROG_SH_PREFIX:-sh2eb-elf}"
export YAUL_ARCH_SH_PREFIX="${YAUL_ARCH_SH_PREFIX:-sh2eb-elf}"
export YAUL_ARCH_M68K_PREFIX="${YAUL_ARCH_M68K_PREFIX:-m68keb-elf}"
export YAUL_OPTION_MALLOC_IMPL="${YAUL_OPTION_MALLOC_IMPL:-tlsf}"
export SILENT=1 NOCOLOR=1

missing=()
command -v make >/dev/null || missing+=('GNU make')
sdk_tools=(gcc as gcc-ar gcc-nm objcopy objdump)
for tool in "${sdk_tools[@]}"; do
    [[ -x "$YAUL_INSTALL_ROOT/bin/$YAUL_PROG_SH_PREFIX-$tool" ]] ||
        missing+=("$YAUL_INSTALL_ROOT/bin/$YAUL_PROG_SH_PREFIX-$tool")
done
sdk_files=(share/build.pre.mk share/build.post.iso-cue.mk share/build.post.bin.mk
    share/build.yaul.mk share/ip.bin.mk
    "$YAUL_ARCH_SH_PREFIX/include/yaul.h"
    "$YAUL_ARCH_SH_PREFIX/lib/libyaul.a" "$YAUL_ARCH_SH_PREFIX/lib/init.o"
    "$YAUL_ARCH_SH_PREFIX/lib/yaul.specs" "$YAUL_ARCH_SH_PREFIX/lib/ldscripts/yaul.x")
if [[ "$elf_only" == 0 ]]; then
    sdk_files+=("$YAUL_ARCH_SH_PREFIX/lib/ip.specs" share/yaul/ip/ip.sx)
    for blob in sys_sec sys_arej sys_aret sys_areu sys_aree sys_init; do
        sdk_files+=("share/yaul/ip/blobs/$blob.bin")
    done
    for tool in make-ip make-iso; do
        [[ -x "$YAUL_INSTALL_ROOT/bin/$tool" ]] || missing+=("$YAUL_INSTALL_ROOT/bin/$tool")
    done
    iso_tool="${MAKE_ISO_XORRISOFS:-xorrisofs}"
    if resolved_iso_tool=$(command -v "$iso_tool"); then
        export MAKE_ISO_XORRISOFS="$resolved_iso_tool"
    else
        missing+=("xorrisofs (or set MAKE_ISO_XORRISOFS)")
    fi
fi
for file in "${sdk_files[@]}"; do
    [[ -f "$YAUL_INSTALL_ROOT/$file" ]] || missing+=("$YAUL_INSTALL_ROOT/$file")
done

if [[ "$check" == 1 ]]; then
    printf 'Source/input check passed for %s. No files written; no compilation performed.\n' "$profile"
    if [[ -z "$rom" && "$probe" == 0 ]]; then
        printf 'A build also requires --rom FILE or --probe.\n'
    fi
    if ((${#missing[@]})); then
        printf 'Build prerequisites still missing (allowed in --check):\n'
        printf '  %s\n' "${missing[@]}"
    else
        printf 'Build prerequisite paths found; tool execution and compilation are unverified.\n'
    fi
    exit 0
fi
[[ -n "$rom" || "$probe" == 1 ]] || fail 'select --rom FILE or --probe; no ROM is included by default'
if ((${#missing[@]})); then
    printf 'error: missing build prerequisites:\n' >&2
    printf '  %s\n' "${missing[@]}" >&2
    fail 'see docs/BUILDING.md; disc packaging requires a lawfully configured Yaul SDK'
fi

build_dir="build/publication/$profile"
output_dir="dist/publication/$profile"
python3 "$project_root/tools/make_build_probe.py" "${input_args[@]}" \
    --output-rom "$project_root/$build_dir/input.gb" \
    --output-assembly "$project_root/$build_dir/input.sx"
title='knovGBC user ROM build'
if [[ "$probe" == 1 ]]; then title='knovGBC build fixture'; fi
make_args=(-C "$project_root" "BUILD_MODE=$profile" "SH_PROGRAM=$program"
    "SH_BUILD_DIR=$build_dir" "SH_OUTPUT_DIR=$output_dir"
    "MVP_ROM_SOURCE=$build_dir/input.sx" "IP_TITLE=$title")
if [[ "$elf_only" == 1 ]]; then
    make "${make_args[@]}" "$project_root/$build_dir/$program.elf"
    printf 'ELF: %s/%s/%s.elf\n' "$project_root" "$build_dir" "$program"
else
    make "${make_args[@]}" package
    printf 'Disc files: %s/%s/%s.bin and %s.cue\n' "$project_root" "$output_dir" "$program" "$program"
fi
if [[ "$probe" == 1 ]]; then
    printf 'The original fixture checks compilation/packaging only; it is not a playable-game or hardware-compatibility test.\n'
fi
