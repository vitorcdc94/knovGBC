# Building knovGBC

The portable entry point selects the existing PicoGB configuration
`mvpfastzoomaudiomono` (057). It changes build input and output paths only.
Historical Make targets and the machine-specific `build_project.*` scripts keep
their original defaults. A publication build never replaces their ROM files or
artifacts.

## Current reproducibility boundary

The project has been built with a locally installed Yaul SDK and a native Linux
SH-2 toolchain: GCC 14.3.0, binutils 2.44, crosstool-NG 1.28.0, and GDB 16.3.
The recorded toolchain build used x86_64 WSL2 Ubuntu with host GCC
15.2.0-16ubuntu1. These identify the existing environment; they do not establish
that a fresh SDK bootstrap works.

The source copy retains eleven tracked build templates from libYaul's component
`build/` directories (`libbcl`, `libfiber`, `libmenu`, `libmic3d`, `libtga`, and
`libyaul`), two `.cxx` sources, and `libyaul/common/wrap-error`. These are
source/build inputs; generated outputs in libYaul's vendor-root `build/`
directory remain excluded. Restoring these inputs does not supply an installed
SDK, resolve the withheld Sega security blobs or the unlicensed external
toolchain configuration, or verify a fresh SDK rebuild.

Yaul's IP template refers to `sys_sec.bin`, `sys_arej.bin`, `sys_aret.bin`,
`sys_areu.bin`, `sys_aree.bin`, and `sys_init.bin`. Those IP security blobs are
intentionally absent from the public source copy while their provenance and
redistribution permissions are unresolved. Full SDK bootstrap and disc packaging
from this public copy therefore remain blocked until a lawful SDK setup is
available. The wrapper does not fetch those files or manufacture replacements.
An existing SDK must be supplied by its user. `--elf-only` avoids the IP template
and blobs and can build the emulator ELF with an already installed SDK.

For external SDK setup information, consult
[Yaul](https://github.com/yaul-org/libyaul) and
[Yaul build scripts](https://github.com/ijacquez/libyaul-build-scripts).
The latter's configuration files are not part of this public export. Their
instructions are upstream setup guidance, not a verified clean-build procedure
for this project. No compiler, SDK, dependency installer, or game ROM is bundled.

## Linux or WSL

Use an x86_64 Linux/WSL environment with Bash and Python 3. Put the checkout in a
path without spaces, for example `~/knovGBC`. Yaul also requires a space-free
SDK path. A copy inside the WSL filesystem avoids Windows drive mount and
permission assumptions.

Source/input preflight needs no compiler, SDK, or ROM and writes no files:

```bash
bash tools/build.sh --check
bash tools/build.sh --check --probe --elf-only
```

`--check` fails for missing source files, unsupported options, space-containing
build/SDK paths, or invalid supplied ROM input. It lists absent build prerequisites
but does not fail merely because an SDK or compiler is absent. A successful check
is not a successful build or a hardware validation.

For an ELF build, supply an already installed Yaul SDK:

```bash
export YAUL_INSTALL_ROOT=/absolute/space-free/path/to/yaul-sdk
bash tools/build.sh --probe --elf-only
bash tools/build.sh --rom /absolute/path/to/my-game.gb --elf-only
```

When a lawfully configured SDK includes the required IP files and the host has
`xorrisofs`, disc packaging can be requested explicitly:

```bash
bash tools/build.sh --probe
bash tools/build.sh --rom /absolute/path/to/my-game.gb
```

Disc builds need GNU Make, Python 3, xorriso's `xorrisofs`, the SH-2 cross tools,
Yaul headers/libraries/specs, and its `make-ip`/`make-iso` tools. ELF-only builds
need the same compiler/library setup but do not require xorriso, `make-ip`, or
IP blobs. `MAKE_ISO_XORRISOFS` may select an explicit executable path. The defaults
are `sh2eb-elf` prefixes, TLSF, and `<checkout>/toolchain`; `YAUL_INSTALL_ROOT`
and the existing Yaul prefix variables may override them. No install or mount
operation is performed.

## Inputs and profiles

No ROM is selected by default. `--rom` and `--probe` are mutually exclusive.
Use only ROMs you have the right to use. A user ROM is copied unchanged into the
isolated build directory and embedded in the resulting executable/disc. Do not
redistribute those artifacts unless you have permission to redistribute the ROM.

| `--profile` | Existing configuration | Required cartridge class |
| --- | --- | --- |
| `mvpfastzoomaudiomono` (default) | 057, mono mixer and SCSP audio | 32 KiB DMG, no MBC, no cartridge RAM |
| `mvpfastzoomaudio` | 056, SCSP audio | Same 32 KiB class |
| `mvpfastzoomtile` | 055, audio disabled | Same 32 KiB class |
| `mvpfastsml` | 051, separate MBC1 configuration | 64 KiB DMG, cartridge type 1, no RAM |

The 32 KiB profiles require header bytes 0x147/0x148/0x149 all zero and the
CGB flag clear. The MBC1 profile requires those bytes to be 1/1/0 and the CGB
flag clear. These are the existing frontend restrictions; the wrapper adds no
mapper, GBC, save, or compatibility support. A compatible header is not proof
that a game runs correctly.

`--probe` creates an original, zero-filled 32 KiB ROM with a small post-boot
idle loop, a `KNOVGBC PROBE` header, and no Nintendo logo or commercial assets.
It exists to exercise compilation and packaging. It does not test game
playability, video, audio, timing, or Saturn/Saroo compatibility. It cannot use
the 64 KiB MBC1 profile.

Generated ROM/assembly, objects, and ELF files stay in
`build/publication/<profile>/`. Disc output stays in
`dist/publication/<profile>/`: `knpub057`, `knpub056`, `knpub055`, or `knpub051`
BIN/CUE pairs. BIN and CUE must remain together. All profiles use an isolated
generated assembly input with the existing ROM symbols; `roms/tetris.gb` and
`roms/Super Mario Land (World) (Rev 1).gb` are never overwritten. Repeating the
same publication profile replaces only that profile's generated outputs.

## Windows launcher

`tools/build.ps1` forwards to Bash in the default WSL distribution and its current
user. `-Distribution` optionally chooses a distribution; `-Toolchain` optionally
selects an SDK directory. It derives paths from the checkout and does not assume
a Windows account name, Linux account name, drive mount, or root access.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1 -Check -Probe -ElfOnly
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1 -Rom C:\ROMs\my-game.gb -ElfOnly -Distribution Ubuntu -Toolchain C:\SDK\yaul
```

The execution policy override applies only to that PowerShell process. Paths are
translated with WSL's `wslpath`. If the Windows checkout contains spaces, the
launcher reports Yaul's path restriction; copy the project into a space-free WSL
directory and run the Bash commands there.

## Host tests without commercial ROMs

These existing tests use synthetic input or mocks. They need Bash, host GCC with
GNU C11/LTO and ASan/UBSan support, GNU coreutils, grep, and diffutils. They do not
need a Saturn SDK or boot ROM and write only their generated test output:

`bash tools/tests/publication_suite.sh` runs the six commands below with
separate logs under `build/publication-tests/` and reports each result.

```bash
bash tools/tests/audio057_apu_test.sh
bash tools/tests/picogb_056_saturn_audio_test.sh
bash tools/tests/vdp2_zoom_test.sh
bash tools/tests/picogb_sm83_contract.sh
bash tools/tests/picogb_ppu_regression.sh
SYNTHETIC_ONLY=1 bash tools/tests/picogb_055_tile_dirty_differential.sh
```

`CC` may select the host compiler; the existing flags target GCC. The synthetic
APU test compares the current code to the frozen 056 fixture. These tests do not
substitute for measurements on Saturn hardware.

Existing gameplay smoke/parity scripts require a user-supplied Tetris or Super
Mario Land image and check those title/header classes. The `--probe` fixture is
not input for those scripts. Commercial test images and their embedded build
artifacts are excluded from the public source export.
