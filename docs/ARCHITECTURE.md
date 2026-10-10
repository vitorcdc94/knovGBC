# Architecture and source map

This describes existing code, not a new implementation.

## Two development paths

`src/saturn/main.c` and `platform.c` integrate SameBoy and load a staged
cartridge through the CD/filesystem path. The default `emulator`/`package`
mode is historical, not the later benchmarked baseline.

Recent `mvpfast*` modes select `picogb_mvp_main.c`, modified PicoGB and one
embedded cartridge. `MVP_FEATURE_MODE` shares cumulative optimizations;
`BUILD_MODE` selects experiments and image identities. The whole Makefile
and unsuccessful experiment sources are retained.

## Recent data path

The Master interprets SM83 instructions and commits machine cycles to
DIV/timers, interrupts, PPU and (056/057) APU. Guarded idle/HRAM shortcuts
skip interpreter work, not device time; events, memory visibility and
interrupt state constrain their validity.

The PPU renders a DMG image into a padded framebuffer. Dirty-scanline and
tile tracking reduce repeated work. VDP2 displays indexed 8-bit pixels with
a four-shade palette. SCU DMA transfers completed regions directly. Zoom
does not change 160x144 logical resolution. Neither VDP1 nor Slave SH-2
renders the current baseline Game Boy image.

With audio enabled, the Master integrates four DMG channels into signed
16-bit mono PCM at 32768 Hz and applies a fixed-point high-pass filter.
Sound I/O synchronizes pending cycles. A main-RAM ring feeds two SCSP pages;
slot 0 plays PCM, while the sound 68000 runs an idle loop. There is no
sound-RAM DMA, time stretching or prerecorded music in this implementation.

## Important files

| Files | Responsibility |
| --- | --- |
| `third_party/PicoGB/microcode*.c`, `mi.*` | SM83 dispatch/state and memory interfaces |
| `third_party/PicoGB/fabric.*` | Memory-mapped I/O, timers and cartridge interfaces |
| `third_party/PicoGB/ppu.*`, `ppu/` | PPU and renderer implementations |
| `third_party/PicoGB/apu.*` | Waveforms, mixing, filtering and PCM |
| `src/saturn/picogb_mvp_main.c` | Recent frontend, startup, frames and metrics |
| `src/saturn/picogb_idle*`, `picogb_hram_wait.*` | Guarded shortcuts |
| `src/saturn/picogb_mvp_input.c` | Fixed controls and joypad interrupt |
| `src/saturn/platform_video.c` | VDP2 bitmap/palette/DMA/zoom |
| `src/saturn/picogb_saturn_audio.*` | Sound stream setup/copy/page monitoring/recovery |
| `boot_entry.sx`, `master_only_cpu_init.c` in `src/saturn/` | Vectors/stacks and Master startup |
| `src/saturn/saroo_bios_shim*.c` | C compatibility shims, not BIOS dumps |
| `src/saturn/picogb_sm83*`, `picogb_slave*`, `platform_video_vdp12.*` | Experimental alternatives |
| `tools/tests/fixtures/audio056/` | Frozen pre-057 APU comparison, LGPL component |
| `tools/tests/` | Host contracts, differentials and hardware mocks |

## Measurements

FPS counts completed logical frames per wall-clock second. Frameskip builds
may also show visual FPS; recent full-rate modes do not. T is total frontend
frame work including pacing/presentation. G is the emulation interval,
including audio generation/servicing when enabled. P is presentation/upload.
C is conversion, and H/X is waiting/pacing depending on HUD mode. Values are
windowed, rounded milliseconds: G+P need not equal T, and G is not CPU alone.

The SH-2 cache is 4 KiB. Function size is not a measured cache miss count;
static instruction counts cannot predict FPS without execution frequency,
memory placement and bus contention.
