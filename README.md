# knovGBC

I recently got a Sega Saturn and decided to create a GB/GBC emulator for it,
so I could play Pokémon and other classics.

I used Codex to help me, providing it with a lot of documentation about the 
Saturn's architecture, since I don't know how to program.

I was able to get Tetris running near full speed without audio, and at about
24.5 logical FPS with audio.
 However, my life is super busy right now and I can't keep working on this 
project. That's why I decided to publish it here so anyone interested can 
continue it.

I named it "knovGBC" after my name, but of course, feel free to change 
it as you like.

I'm sorry for not being able to properly understand the code as I should, 
since I don't know how to program. Codex did everything, including those
 files, so if any error occurs, talk to me so I can fix it. 

And if you want to keep working on it and have any doubt, feel free to 
message me so I can get any needed info from Codex.

At last, here's the technical info:



An experimental Game Boy emulator for the Sega Saturn, published as a source
archive so other developers can study the port and continue the work.

**Incomplete and no longer maintained by the original developer.** This is
not a finished emulator or a promise of compatibility. Forks are welcome;
there is no commitment to support, issue response or further releases.

The name reflects the original GB/GBC goal. The recent tested development
path is **DMG Game Boy only**. The historical SameBoy-based GB/GBC port is
retained, but working Game Boy Color support on Saturn has not been demonstrated.

## Included in the source handoff

- Saturn frontend, startup code, hardware experiments and build modes.
- Modified PicoGB sources, older SameBoy integration, selected third-party
  sources, licensing notices, synthetic tests and historical reports.
- Portable build helpers and an original synthetic compilation probe.

Commercial cartridges, game-containing disc images, Nintendo BIOS dumps,
installed compilers, private logs and unreviewed Sega security blobs are not
included in the public package. The exporter does not delete local originals.
See [Publication](docs/PUBLICATION.md) for the precise release boundary.

## Architecture

Recent `mvpfast*` modes use a modified **PicoGB** C interpreter compiled for
the big-endian Master SH-2 with GCC `-O3`/LTO. **libYaul** supplies the Saturn
runtime and interfaces. The original SameBoy frontend remains a separate
historical path; `make package` without a mode still selects that old path.

| Component | Role in the recent baseline |
| --- | --- |
| Master SH-2 | SM83 interpretation, memory, PPU, input and audio synthesis |
| VDP2 | Indexed 8-bit framebuffer display, aspect-preserving zoom |
| SCU DMA | Framebuffer-region transfers to VDP2 VRAM |
| SMPC | Controller input |
| SCSP | Mono PCM playback in audio builds |
| Sound 68000 | Minimal running idle program, not sound synthesis |
| Slave SH-2 / VDP1 | Historical experiments, not used for recent GB rendering |

This remains an emulator: it interprets cartridge SM83 instructions at runtime.
Embedding a ROM in a Saturn image does not decompile the game.
See [Architecture](docs/ARCHITECTURE.md) and [History](docs/HISTORY.md).

## Functionality and evidence

The recent path implements DMG CPU/memory/interrupt/timer handling, 160x144
software graphics, fixed controls and automatic startup of one embedded ROM.
Zoom enlarges the existing pixels; it does not increase logical resolution.
Modes 054-057 render every logical frame without frameskip. Earlier modes
retain their own frameskip and experimental configurations.

056/057 add experimental four-channel DMG audio: signed 16-bit mono at
32768 Hz, fixed-point filtering and SCSP buffering. There is no recent-path
ROM browser, persistent saving, save states, rewind, configurable controls,
RTC or validated GBC mode. Upstream features are not Saturn port validation.

These are observations **reported by the developer**, not universal benchmarks
or hardware tests performed during publication preparation:

| Build / mode | Reported observations |
| --- | --- |
| 050 / `mvpfastdirtyregions` | Tetris around 59 logical FPS with frameskip; controls/stability tested |
| 051 / `mvpfastsml` | Separate Super Mario Land/MBC1 trial, around 20.5 logical FPS, scene-dependent |
| 055 / `mvpfastzoomtile` | Tetris without audio or frameskip, approximately 53-59 FPS |
| 056 / `mvpfastzoomaudio` | Audio enabled: about 24.5 FPS, T042/G040/P001 |
| 057 / `mvpfastzoomaudiomono` | Equivalent mono mixer; no observable performance gain over 056 reported |

The latest build number is **057**, not necessarily the fastest baseline.
050/055 remain useful no-audio references. Audio quality/synchronization have
not been comprehensively validated. Host tests established exact 056/057 PCM
parity and matching state/image traces over 3600 scripted Tetris frames;
that does not prove accuracy for every cartridge.

## Build and run

Use Linux or WSL2, with a checkout path **without spaces**. Dependencies:
Bash, GNU Make, Python 3, host GCC for tests, big-endian `sh2eb-elf`
GCC/binutils and a compatible installed libYaul SDK. Disc packaging also
requires Yaul tools and `xorrisofs` (xorriso). The installed toolchain is not
redistributed. Observed target versions were GCC 14.3.0 and binutils 2.44.

```bash
# Synthetic host tests: no commercial ROM required.
bash tools/tests/audio057_apu_test.sh
bash tools/tests/picogb_056_saturn_audio_test.sh
bash tools/tests/vdp2_zoom_test.sh

# Original compilation fixture, not a playable game or a hardware validation.
YAUL_INSTALL_ROOT=/path/to/installed/sdk bash tools/build.sh --probe --elf-only

# Private build with a ROM you have the right to use.
YAUL_INSTALL_ROOT=/path/to/installed/sdk bash tools/build.sh \
  --profile mvpfastzoomaudiomono --rom /path/to/your/rom.gb
```

Tetris-class modes require exactly **32 KiB, DMG, ROM-only cartridge type 0,
no cartridge RAM**. Arbitrary GB/GBC ROMs are not supported by this narrow
path. The separate SML mode has 64 KiB/MBC1 constraints. Build helpers do not
download cartridges.

The public export withholds libYaul's Sega bootstrap/security blobs because
redistribution permission was not established. Full fresh SDK/disc bootstrap
has a documented dependency/legal impediment; it is not claimed as a
one-command reproduction. ELF compilation can be verified with an existing
SDK without generating a disc security header. See [Building](docs/BUILDING.md)
and [Release verification](docs/RELEASE_AUDIT.md) for exact commands and limits.

Keep a locally built BIN/CUE pair together. Historical hardware tests used a
Saturn with SAROO; copy lawful private images to the loader's configured image
directory, commonly `SAROO/ISO/`. Stock-console booting, other loaders, Saturn
emulators and PAL behavior are not comprehensively validated. Do not upload
images containing commercial games or unreviewed security code as releases.

Current PicoGB controls: Saturn D-pad -> GB D-pad, A -> A, B -> B,
Start -> Start, **X -> Select**. Historical SameBoy modes differ.

## Handoff and known problems

Audio causes a major slowdown; the 057 mixer change did not recover it.
Compatibility coverage is narrow and many choices are deliberately DMG-only.
The retained assembly/Slave/VDP1 experiments did not establish a better recent
baseline. Sound buffers can underrun below real-time speed.
See [Known issues](docs/KNOWN_ISSUES.md).

Most integration scripts need private Tetris/SML inputs; some explicitly
check cartridge titles. Public synthetic tests do not. Historical reports have
been translated into English and refer to earlier snapshots. The original working folder
has no root Git history; this handoff does not manufacture missing commits.

## Licenses

The existing [MIT license](LICENSE) is retained for independently authored
knovGBC code/documentation. **It is not a blanket license for this tree.**
SameBoy Core/BootROMs use Expat/MIT; libYaul uses MIT with file-specific
exceptions; the active modified PicoGB library uses **LGPL 2.1**. Applicable
LGPL terms remain with PicoGB-derived code. Historical references have
additional licenses.

The libYaul PRS files retain comments reporting free use with attribution to
fuzziqer, but no formal grant is bundled and the primary permission has not
been independently verified. They are a separate source exception, not
relicensed by this archive's MIT or zlib-style notices.

Preserve third-party notices and modification records. Static PicoGB binary
distribution requires LGPL compliance, including ability to rebuild/relink;
this source does not authorize distribution of commercial games or Sega blobs.
See [Third-party licenses and provenance](docs/THIRD_PARTY.md).
Trademarks belong to their respective owners.
