# kmvpe044 — experimental presentation using SCU DMA

Date: 2026-09-25. The hardware reference remains kmvpe041: 47.9 logical FPS
(23.9 visual FPS), T21 G16 P06. kmvpe044 has not yet been measured on Saturn.

## Isolated change

Same configuration, GB core, PPU, scheduling, frameskip, ROM and controls as
kmvpe041. Only image presentation changed: the Master prepares 144 RGB1555
lines of 160 pixels in Work RAM-H, and level 2 indirect SCU DMA transfers
each line to the same VDP2 NBG0 bitmap. The HUD and image position were not
changed. The Master waits for DMA completion before releasing the buffer.
The PPU's 176-pixel source line remains intact.

The table contains 144 descriptors of 12 bytes, aligned to 2,048 bytes,
with only the last marking the end. Each descriptor transfers 320 bytes;
destination addresses have a 1,024-byte stride. Channel 2 is used only
during presentation: libYaul's VDP2 register commits finish during
initialization. If the program starts calling `vdp2_sync()` during
gameplay, this exclusive usage must be reviewed.

There is no global purge per frame: the SH-2 cache is write-through, so
pixels written by the Master reach the RAM read by the SCU. A compiler
barrier precedes DMA activation, and a synchronous wait occurs before
reusing the area. This still requires confirmation on real hardware.

## Checks performed

- SH-2 cross-compilation and BIN/CUE packaging: completed. The header
  identifies `T-99947G`, `V0.054` and `knovGBC VDP2 DMA 044`;
  the ISO contains a `CD001` descriptor.
- Host test using the real `platform_video.c` and a simulated SCU/VDP2:
  passed with `-O3 -flto` and ASan/UBSan. Compares all VRAM against a
  reference presentation, including HUD, borders, 144 descriptors, alignment,
  RGB1555 bit, stride 176, stride 160 and a source aligned to only 2 bytes.
  Also checks that DMA completes before returning.
- The 041 and 044 preprocessed files are byte-identical across all core
  and Saturn modules except `platform_video.c` and `picogb_hram_wait.c`.
  For the latter, removing only `#line` location directives and whitespace
  also yields identical C tokens; those directives changed because experimental
  modes 042/043 were defined in the same file after 041 was generated.
- `dist/kmvpe041.bin` remained unchanged; SHA-256
  `A677FE3F82ADF0EB28813DBF2D50FC69835BE59B1F9E3DD3F6DBCFC67EA2E35C`.

## Artifacts and decision

- `dist/kmvpe044.bin`: 655,360 bytes, a multiple of 2,048; SHA-256
  `713CCC87C5D75B84272A0CF5DB2EC6B23B29A17C764BA5999C217604B84E596A`.
- `dist/kmvpe044.cue`: references `kmvpe044.bin`, MODE1/2048 track.

Test kmvpe044 on Saturn for a few minutes. First check startup, the full
image, HUD and controls; then record logical/visual FPS and T, G, P on the
title screen and during gameplay. A host test does not measure VDP2 bus
contention or the real SCU DMA cost; adopt 044 as the new baseline only
if hardware confirms improvement or a tie without defects.
