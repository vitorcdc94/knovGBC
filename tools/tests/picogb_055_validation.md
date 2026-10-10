# Build 055 validation

Completed on 2026-10-03. Test candidate; performance not yet validated
on a physical Saturn.

## Isolated change

055 (`mvpfastzoomtile`, `kmvpz055`) derives from 054: the same Tetris ROM,
C interpreter with `-O3`/LTO, Master SH-2, indexed8/DMA VDP2 output, zoom,
three-digit HUD and no frameskip, audio or saves.
The only additional functional option is `KNOVGBC_PPU_TILE_DIRTY_LINES=1`.

Each rendered line records the physical tiles used by BG, window and sprites.
A pattern change in VRAM invalidates only the lines that used that
tile. Structural changes to maps, OAM and registers retain the invalidation
policies of 054. CPU and PPU state/timing were not changed.

The table requires 6,912 additional BSS bytes and the executable gains 240 bytes
of `.text`. Looking up/rebuilding dependencies also costs processing time:
fewer redraws do not guarantee an FPS improvement on hardware.

## Checks performed

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_project.ps1 -Target fullratetest
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_project.ps1 -Target mvpfastzoomtile
```

- 3,600 Tetris frames with automated inputs: 054 and 055 produced identical
  cycle, instruction, interrupt, memory, state and image traces.
- Synthetic cases covered BG/window, scrolling, maps, signed tile addressing,
  mid-frame writes, 8x16 sprites, both halves with/without flipping, OAM
  position/tile changes and dependencies rebuilt after map changes.
- Synthetic cases also ran with AddressSanitizer and UBSan, without errors;
  outputs matched the optimized builds and the 054 reference.
- Avoided-work assertions: changing one isolated tile invalidated 8 lines
  in the candidate versus 144 in the reference; a non-visible tile invalidated 0
  versus 144. The last table bit (physical tile 383) was also exercised.
- SH-2 compilation completed. Existing upstream core warnings and the linker's
  RWX segment were not changed by this optimization.
- BIN aligned to 2,048-byte sectors, valid Saturn/ISO9660 headers,
  JUE regions, relative CUE, `1ST_READ.BIN` as the first ISO file and
  byte-identical to the compiled executable. No additional ROM in the ISO.
- Static end `0x060384E8`, Master stack `0x060FFFF0`, no static
  overlap. This does not measure peak stack usage at runtime.

## Artifacts and preserved references

Package delivered as `dist/dist Tetris/kmvpz055.bin` and `kmvpz055.cue`.
SHA-256 of the BIN delivered in this build:
`6E13C2F0F0F2C23896B5F687ED6B54C35C4E3E51E375A391025C1A9DE882DC03`.
Internal ISO timestamps may change the hash when rebuilding the package.

Baselines were not rebuilt or overwritten; hashes checked:

- 053: `58643411BC04D1121FD28E53E2CFDAE92C0227B2526E951E0A069E7F2A5CF5C2`.
- 054: `9F0A404F9C9123269A30E83233B22DE4738BD6A3EF2519AA4AC121BCC800091D`.

## Next test

Compare 055 with the 50–55 gameplay FPS reported for 054, using the same
configuration, including several minutes of gameplay, pausing and resuming.
Observe FPS, T/G/P, controls and any stale lines/blocks.
Do not adopt the candidate as the new baseline before testing on hardware.
