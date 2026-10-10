# kmvpl038 — sending dirty lines to VDP2

Saturn-approved baseline: **kmvph036**, 43.5 logical FPS / 21.8
visual FPS, T0023 G0020 P0006. 037 tied and is **not** included in 038.

## Isolated change

The Master SH-2 continues executing exactly the 036 core with `-O3`,
PicoGB in C, its HRAM wait fast paths, embedded Tetris, frameskip 1 and
160×144 output through VDP2. Only `platform_video.c` receives
`KNOVGBC_VDP2_DIRTY_LINES`: it compares each line's 160 visible pixels
against the last **presented** image and writes only changed lines to VDP2.
No line is discarded for being even or odd. The first frame after
initialization writes all 144 lines. The history buffer uses 46,080 bytes
of Work RAM; no VRAM byte is read for comparison.

## Internal checks

- `tools/tests/vdp2_dirty_lines_test.sh`: **PASS** with O3/LTO and
  ASan/UBSan. Runs the real implementation with simulated VRAM and compares
  output after each presentation against a full transfer. Includes the
  initial frame, repeated frame, one changed line, reversion, all lines,
  padding, aligned/misaligned source, bit 15, borders, lines 71/72 at the
  A0/A1 boundary, frameskip and reinitialization. VRAM sentinels confirm
  that unchanged lines are not rewritten and different lines are sent.
- `tools/tests/picogb_hram_wait_differential.sh`: **PASS** across 600,
  1,800 and 3,600 frames; cycles, state, memory and image identical per
  frame in the 036 core. At 3,600: 26,972,874 equivalent instructions,
  63,512,429 M-cycles, framebuffer `d90ed43cb756538f` and state
  `7d13978c673b8585`.
- Independent review of isolation and visual output: no blockers found.
  Comparison and copying have their own cost; the gain cannot be inferred
  from host tests.

## Saturn package

- `dist/kmvpl038.bin`: 651,264 bytes, ISO9660 with PVD `CD001`.
- `dist/kmvpl038.cue`: MODE1/2048, referencing the same BIN.
- Header: `T-99953G`, `V0.048`, `knovGBC VDP2 Dirty Lines 038`.
- `1ST_READ.BIN` at LBA 33: 270,336 bytes, identical to the generated executable.
- Embedded ROM: 32,768 bytes identical to `roms/tetris.gb`.
- SHA256 BIN 038:
  `E17BFCBF60AB0DF416EF3394437BC15ECA069DED0EF3E63A6C3B1A95E39DC200`.
- SHA256 CUE 038:
  `87F62A41236B31CA2ADFA5A1C2601EC66CBAEEBCB049B943C75490D2A42A5810`.
- SHA256 of unchanged earlier BINs: 036
  `EDCD6E49BBE740BEE2F3387A9DC06299B734C7EB5C58CC1551B4364BA52A9A3E`;
  037 `DD05E68FC806C15866B92E42FB394407FE593299DBF64C3BE1C6747E9BF8D5E2`.

## Hardware test

Copy BIN and CUE together to Saroo. Compare against 036 at the same Tetris
stage after readings stabilize, recording logical/visual FPS, T, G and P.
Check the title screen, moving pieces, controls and a few minutes of
gameplay. Nearly static screens may save more lines; scrolling or full-screen
animation may not improve and could even regress, because comparing 46,080
bytes and copying changed lines costs CPU/cache work. Keep 036 as the
baseline until Saturn confirms real improvement and stability.
