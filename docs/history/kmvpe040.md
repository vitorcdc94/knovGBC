# kmvpe040 — two-line PPU batching candidate

Preserved baseline: kmvpe039, measured on Saturn at 44.9 logical FPS,
22.4 visual FPS, T22 G18 P06. 040 adds only
`KNOVGBC_PPU_TWO_LINES`; it does not include experiments 037/038.

During the safe HRAM wait loop, the PPU can receive the time required to
complete two visible lines in a single call. It still internally processes
and renders every transition. The extension is accepted only through line
141, with no STAT sources enabled or pre-scheduled IRQ, and never crosses
VBlank. At line 142, it uses the one-line 039 fast path; in other cases,
the previous paths remain. The maximum deadline is 912 dots, reached at the
first instruction boundary through up to 32 complete loop iterations and
part of the next one. SM83, timer, VDP2, audio, saves and ROM were unchanged.

Host checks (Saturn PPU configuration, interlace 1):

- Contract: 7,844 cases passed with -O3/LTO for 039 and 040, and with
  ASan/UBSan for 040. Includes line 141–143 boundaries, STAT sources,
  an already scheduled interrupt, end of frame and continuation after
  the fast path.
- 039/040 differential: equality at every one of 600, 1,800 and 3,600 frames,
  including gameplay input. Cycles, instructions, state, memory and
  image matched.
- Over 3,600 frames, external HRAM wait groups: 493,127 (039) versus
  256,106 (040), a 48.06% reduction. Both finished with 26,972,874
  instructions and 63,512,429 emulated machine cycles. Internal
  PPU rendering work was not eliminated.
- The `picogb_hram_wait_try` symbol and two-line branch appear in the
  final ELF. The other preprocessed C files for the SH-2 target are
  identical to 039.

Saturn artifacts: `dist/kmvpe040.bin` and `dist/kmvpe040.cue`.
SHA-256 of the BIN:
`B7F6F4CA13F8C91AF53A084FF18DBE86C0C7F73EB9B55F42A18D623459DDA1BA`.

Fewer groups do not guarantee higher FPS on hardware. Compare 040 with
039 under the same conditions, on the title screen and over a few minutes
of gameplay. The 039 files in `dist` were not overwritten.
