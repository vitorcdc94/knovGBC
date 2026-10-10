# kmvpe041 — adaptive HRAM/PPU wait batching

Date: 2026-09-25. kmvpe040 remains the hardware reference: 47.9 logical FPS
(23.9 visual FPS), T21 G17 P06. kmvpe041 has **not** yet been measured on Saturn.

## Isolated change

The same ROM wait loop, which polls HRAM without modifying it, can now advance
through the first instruction completed after the PPU enters LY=144
(VBlank/end of frame), instead of using a fixed number of lines. The fast
path is used only with the LCD enabled, TIMA timer disabled, no STAT IRQ
source enabled, no already scheduled PPU IRQ and no pending IRQ that can be
delivered. Otherwise, the previous event-based path remains. `ppu_tick()`
still processes all lines, mode changes and intermediate rendering.
SM83 CPU, VDP2, frameskip, ROM and controls were not changed.

Only `picogb_hram_wait.i` differs between the preprocessed 040 and 041 builds.
The `picogb_hram_wait_try` symbol was confirmed in the final 041 map.
040 was not rebuilt or overwritten.

## Checks performed

- SH-2 build `mvpfastppuadaptive`: completed, with BIN/CUE.
- 041 contract: 584 cases against instruction-by-instruction SM83 execution,
  including lines 0, 100 and 141–143, cycle boundaries, LYC, STAT and pending
  IRQ; identical CPU/PPU state, cycles, IRQ and pixels. The ASan/UBSan
  build passed.
- Previous 040 contract: 7,844 cases passed again, including ASan/UBSan.
- 040/041 differential: identical per-frame traces over 600, 1,800 and 3,600
  frames, including gameplay input. Covers cycles, state, memory and image.
  SHA-256 of the 3,600-frame trace for both:
  `990B09596E1C6C669F774D214B648DA37C6FE14C208185D925909069803066DE`.
- Over 3,600 frames, external HRAM/PPU wait groups fell from 256,106 to
  9,209 (−96.4%); admission calls and actual SM83 instructions were identical.
  This does **not** predict the FPS change, since the PPU still draws all lines.

## Artifacts

- `dist/kmvpe041.bin`: 606,208 bytes, a multiple of 2,048; SHA-256
  `A677FE3F82ADF0EB28813DBF2D50FC69835BE59B1F9E3DD3F6DBCFC67EA2E35C`.
- `dist/kmvpe041.cue`: references `kmvpe041.bin`, MODE1/2048 track.
- `dist/kmvpe040.bin` preserved; SHA-256
  `B7F6F4CA13F8C91AF53A084FF18DBE86C0C7F73EB9B55F42A18D623459DDA1BA`.

## Next decision

Test 041 on Saturn, on the title screen and during a few minutes of gameplay.
Compare logical/visual FPS, T, G, W and P with 040. Adopt 041 as the new
reference only if it improves or matches performance without visual,
control or stability anomalies.
