# kmvpe045 — SCU DMA directly from the emulator frame

Date: 2026-09-25. Experiment for comparison with hardware reference
kmvpe041 (47.9 logical FPS, 23.9 visual FPS; T21 G16 P06) and the
intermediate-buffer DMA in kmvpe044 (47.9/23.9; T21 G16 P07, according to
the user's reported test). kmvpe045 has **not yet been measured on Saturn**.

## Isolated change

The GB core, PPU, scheduling, controls, ROM and frameskip retain the
kmvpe041 configuration. During presentation, 144 level 2 SCU DMA descriptors
read the 160 visible pixels directly from each 176-pixel `core_frame` line
and write 320 bytes per line to the VDP2 NBG0 bitmap. The Master waits for
DMA completion before executing the next frame. The 46,080-byte intermediate
buffer from kmvpe044 was removed. If the source is not 4-byte aligned or
has an odd stride, the CPU presentation path remains.

DMA does not modify the PPU's RGB1555 pixels. Bit 15 of those pixels can be
zero: `VDP2_SCRN_DISP_NBG0 = 0x0101` keeps N0TPON active, meaning the
transparency code is disabled for NBG0. A compile-time assertion protects
this condition. Consequently, kmvpe045 VRAM is not byte-identical to
kmvpe044 VRAM, which set bit 15 during the intermediate copy; colors should
be equivalent on VDP2. Visual confirmation on the console is still required.

## Completed checks

- Isolated test using the real `platform_video.c`: **PASS** with `-O3 -flto`
  and ASan/UBSan. Checks direct sourcing, stride 176/160, 144 descriptors,
  length/destination/end, intact source, synchronous wait, HUD, borders,
  pixels with bit 15 clear and fallback for a misaligned source.
- Regression of 044's intermediate DMA: **PASS** with `-O3 -flto` and
  ASan/UBSan.
- SH-2 cross-compilation and BIN/CUE package: completed. The header contains
  `T-99946G`, `V0.055`, `knovGBC VDP2 DMA Direct 045`; the ISO contains
  `CD001`, and the BIN size is a multiple of 2,048 bytes. The executable
  map contains the DMA table and does not contain `dma_staging`.
- The 041 and 045 preprocessed files are identical in the other core
  modules, except `picogb_hram_wait.c`, whose C tokens are identical after
  ignoring only `#line` directives and whitespace. `platform_video.c`
  is the intentionally changed module. Existing 041 and 044 BINs remained
  unchanged.

## Files and hardware test

- `dist/kmvpe045.bin`: 610,304 bytes; SHA-256
  `A06183CEC79335F868C21783A1D1A930DB28271E4036E05B76C5EB51AFCD9108`.
- `dist/kmvpe045.cue`: references `kmvpe045.bin`, MODE1/2048 track.
- Reference 041 SHA-256
  `A677FE3F82ADF0EB28813DBF2D50FC69835BE59B1F9E3DD3F6DBCFC67EA2E35C`.
- Experiment 044 SHA-256
  `713CCC87C5D75B84272A0CF5DB2EC6B23B29A17C764BA5999C217604B84E596A`.

On Saturn, first check boot, startup markers, the full image, HUD, colors,
controls and stability over a few minutes. Then compare logical/visual
FPS and T/G/P with 041 and 044 in the same gameplay situation. Removing
the copy saves Master work, but bus contention and DMA cost can only be
judged on hardware. Keep 041 as the baseline until this comparison.
