# kmvpe046 — nonblocking controller request when the SMPC is busy

The user confirmed `kmvpe045` stable on Saturn, and it remains the
reference: 53.4 logical FPS (26.7 visual FPS), T19 G16 P02. `kmvpe046`
is only an input experiment; no hardware result is available yet.

## Isolated change

At the start of each logical frame, the program still processes the last
completed collection, reads the port 1 digital controller and generates
the Game Boy joypad interrupt only on new button presses. Afterward, it
requests another `INTBACK` collection only if the SMPC `SF` busy bit is
clear. If still busy, it keeps the last reading and tries again on the
next frame, without entering Yaul's command-issuance wait.

The SMPC command mode, issuance timing, GB frame frequency, button mapping,
SM83/PPU core and direct VDP2 DMA were not changed. V-blank command timing
can be studied separately; changing it here would interfere with the
comparison against 045.

## Completed checks

- Real input-module test with a simulated SMPC: **PASS** with `-O3 -flto`
  and ASan/UBSan. Covers the busy bit, collection ready before `SF` clears,
  held buttons, new presses, release, disconnection and all eight mapped
  buttons.
- Regression of the 045 direct-DMA presenter: **PASS** with `-O3 -flto`
  and ASan/UBSan.
- SH-2 cross-build and BIN/CUE package: **PASS**. Image header:
  `T-99945G`, `V0.056`, `knovGBC SMPC Gate 046`.
- Of the 14 preprocessed C modules in 045 and 046, 13 are identical;
  only `picogb_mvp_input.c` differs.
- `dist/kmvpe045.bin` retains SHA-256
  `A06183CEC79335F868C21783A1D1A930DB28271E4036E05B76C5EB51AFCD9108`.

## Files and Saturn test

- `dist/kmvpe046.bin`: 610,304 bytes; SHA-256
  `C522C7674164B941674B149B5521E5708CADF95630D7DA393B1BF9E2BFF3BBC8`.
- `dist/kmvpe046.cue`: single `MODE1/2048` track referencing that BIN.

Compare 046 and 045 on the same screen and during a few minutes of gameplay.
Record logical/visual FPS and T/G/P, but prioritize controller response:
test quick taps, held buttons, release, Start and X (Select). Any FPS
improvement does not justify perceptible input lag or lost commands.
Keep 045 as the baseline until Saturn confirms improvement and intact controls.
