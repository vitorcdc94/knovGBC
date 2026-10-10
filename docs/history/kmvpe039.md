# kmvpe039 — PPU event-coalescing candidate

Base: kmvph036 (Master SH-2 + VDP2, -O3/LTO). Does not include experiments
037 (CP n8) or 038 (VDP2 dirty lines). The presentation path without 038 was
restored to the effective 036 source code.

During the already recognized HRAM wait loop, 039 can advance transitions
from mode 3 through the start of the next visible line in a single
`ppu_tick` call. The PPU still performs every internal transition. The fast
path is used only before line 143, with STAT sources disabled and no
pre-scheduled interrupt; other states use the 036 logic. Timer, SM83, audio,
saves and ROM loading were not changed.

Host checks:

- 039 contract: 7,681 cases passed with -O3/LTO and ASan/UBSan; 412 cases
  demonstrated coalescing. 036 contract: 7,036 cases passed.
- 036/039 differential: frame-by-frame equality across 600, 1,800 and 3,600
  frames, including gameplay input. Cycles, instructions, state,
  memory and image matched.
- Over 3,600 frames, external HRAM wait groups: 1,446,595 (036) versus
  493,127 (039), a 65.91% reduction. Internal PPU transitions were not
  suppressed. Both completed 26,972,874 instructions and 63,512,429
  emulated machine cycles.
- The `picogb_hram_wait_try` symbol and new branch are present in the
  final ELF, not removed by link-time optimization.

Test artifacts: `dist/kmvpe039.bin` and `dist/kmvpe039.cue`.
SHA-256 of the final BIN:
`C92A48C9BB0D77F05274DE58012B8B0DFA88BD60724B8A1732B1BD6D3D55DAB9`.

An FPS gain cannot be inferred from the reduction in groups: measure on
Saturn and compare with 036, including a few minutes of gameplay. Baseline
036 and its files in `dist` were not overwritten.
