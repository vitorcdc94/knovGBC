# Build 057 validation: APU mono mixer

Completed on 2026-10-04. Candidate for hardware comparison with 056;
no Saturn FPS result for 057 is available yet.

## Single change

`mvpfastzoomaudiomono` / `kmvpa057` keeps the 056 configuration and
adds only `KNOVGBC_APU_MONO_FAST=1`. The Master still synthesizes
the four DMG channels into signed16 mono PCM at 32768 Hz. CPU, PPU, controls,
zoom, HUD, presentation without frameskip and the SCSP backend were not changed.
The filter, buffers, frequency and oscillator advancement also remain unchanged.

The mixer no longer builds the left and right terminals separately and then
adds them together. It calculates the same mono area directly, using four
coefficients, a DAC offset and a DAC-enabled flag.
These values are recalculated only on relevant writes to NR50/NR51,
NR12/NR22/NR30/NR42, initialization and power-off/reset. Writes still
synchronize pending time before updating the registers.

If `digital_i` is a channel's digital area over the interval `dots`, the
identity used is `sum(weight_i * (15*dots - 2*digital_i)) =
dots * (15*sum(weight_i)) - 2*sum(weight_i*digital_i)`. A disabled DAC
has zero weight. An enabled but unrouted DAC still contributes to the
flag used by the filter. All oscillators continue to advance,
including those with zero weight. No audio is skipped and the GB clock is not accelerated.
Area calculations remain signed and rounding takes place at the same
point as in 056, including when a write occurs in the middle of a sample.

The optimized path adds 24 bytes to the APU state. With the macro disabled,
or in stereo mode, the original structure and mixer are preserved.
This is not a Tetris-ROM-specific optimization.

## Reproducible checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_project.ps1 -Target audiomonotest
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_project.ps1 -Target mvpfastzoomaudiomono
```

The immutable fixture `tools/tests/fixtures/audio056/apu.c/.h` contains
the code from before the change. Tests compile both versions with
independent symbols, without replacing the previous binary.

- Differential tests for optimized mono, mono with the flag set to zero and
  stereo with the flag enabled: all passed with both O3/LTO and ASan/UBSan.
- Each mono run performed 7,287,149 state checks and compared
  1,239,873 exact PCM entries; FNV-1a `a0144e0073b1910c`.
- Each stereo run compared 2,479,742 exact PCM entries;
  FNV-1a `6326e49cf5e5c538`.
- Coverage: 65,536 NR50/NR51 combinations, DAC and power transitions,
  DIV, envelope/sweep/wave/noise, writes within samples, filtering with
  unrouted channels, disabled rendering, time partitions, producer wraps
  and rings of different capacities, including non-powers of two.
- The internal mono area is compared as L+R, since 057 deliberately uses
  a single accumulator; its right accumulator is checked as zero.
  The additional cache is not part of the binary state comparison.
- Tetris integration: four runs of 3,600 frames with automated gameplay
  inputs (056 fixture, current path with the flag set to zero,
  optimized mono and sanitized mono). All CPU, cycle, interrupt, memory
  and image traces, and all PCM summaries, matched.
- Integration PCM: 1,984,763 samples, 1,951,070 nonzero, minimum
  -26188, maximum 25276; FNV-1a `206626b0920e59da`, matching 056.
- Independent review of the cache and I/O boundaries: no blockers.
- SH-2 O3/LTO build completed. Existing upstream qualifier/parameter
  warnings and the linker's existing RWX warning remain.

## Generated code and package

In the final ELF, `advance_cycles.constprop.0`, which includes the hot mixer,
went from 1508 to 1308 bytes (-200). Coefficient updates occupy 220 bytes
outside that path; `apu_write` went from 1224 to 1256 bytes.
These sizes demonstrate a change in generated code, not a measured Saturn
runtime improvement or evidence that the whole emulator fits in the cache.

In the inspected mixing section of the linked assembly, mixer conditional
branches were eliminated and word-access operations were reduced.
However, the new section has five variable multiplications versus two
in the previous one. This is a trade-off in work and code size, not proof of
fewer actual cycles. The preprocessed SCSP driver remains identical between
the two builds, as do the frequency, buffers and playback MMIO sequence.

Disc BIN: 612352 bytes, 299 sectors of 2048 bytes. CUE: 71 bytes,
relative reference to `kmvpa057.bin`, MODE1/2048 and index 00:00:00.
Saturn signature, product `T-99934G`, version `V0.067`, JUE regions,
title `knovGBC Tetris Audio Mono 057` and ISO9660 PVD `CD001` checked.

`1ST_READ.BIN;1` is the first regular file in the root directory, at sector 33.
Its 231424 bytes exactly match the compiled executable;
SHA-256 `8BBCA7D2619C136F100A2338C3839FAF09FFA75EE7457B8A02237DD8A58AEDA6`.
The ROM embedded at `0x06016250` matches the 32768 bytes of
`roms/tetris.gb`; SHA-256
`0D6535AEF23969C7E5AF2B077ACADDB4A445B3D0DF7BF34C8ACEF07B51B015C3`.

`.bss`: 122160 bytes. Static end: `0x0603C5A8`; Master stack:
`0x060FFFF0`. No static overlap; this does not measure actual peak stack usage.

Delivery: `dist/dist Tetris/kmvpa057.bin` and `kmvpa057.cue`, kept together.
BIN SHA-256:
`266F7FD4C929515CE18EFFD9ECF3AB6D2ABFA4B637668D4E3D7EBD04065DF2CC`.
CUE SHA-256:
`57A969AD6FEB8445CB668F0A07649DFAB9FF0C9BE9438B75518A4B7759B859DF`.

## Preservation and hardware testing

050, 055 and 056 were not rebuilt or overwritten. Hashes checked:

- 050 BIN: `7946E850463A1491F392FB8A4E6B368576D3EAD9E0A22DF25334D996351E84B2`.
- 055 BIN: `6E13C2F0F0F2C23896B5F687ED6B54C35C4E3E51E375A391025C1A9DE882DC03`.
- 056 BIN: `A1B46D9BBBF1A862291FC5705DE3AB7CD17AD985700A19096B688AD666EBAC8C`.
- 056 CUE: `51BA44BC407B6C6C32309ABFBA408EFA4FA75003DA92B8B50210561320164B16`.

Compare 057 with 056 during the same gameplay section and with the same
music selection. Reported hardware reference: approximately 24.5 FPS,
T042/G040/P001. Record FPS/T/G/P, stability, music and effects,
clicks, silence or distortion. Exact host PCM comparison does not
simulate bus contention or actual SCSP playback. Therefore,
speed and sound continuity can only be confirmed on hardware.
