# kmvpe031 — waiting through the first event boundary

Generation date: 2026-09-22.

Subsequent Saturn test reported by the user: **36.6 logical FPS / 18.3
visual FPS, T0027 G0024 P0006**. Approved as the new baseline. The original
BIN/CUE remains preserved with the hashes below.

## Reference and isolated change

Preserved hardware reference: kmvpi029, 31.1 logical FPS / 15.5 visual FPS,
T0032 G0029 P0006. Does not include the kmvpr030 recognition cache.

Same C interpreter, -O3, Master SH-2 + VDP2, frameskip 1, embedded
Tetris ROM, no audio or saves. Only functional change: the
`F0 a8 A7 28 FB` fast path, reading zero from HRAM, now includes the first
instruction that reaches the PPU event. Previously, it stopped strictly
before the event.

If the cutoff occurs after F0 or AND, it records the exact partial PC/IR/flags.
DIV and PPU are updated before any next instruction, and the interrupt is
delivered through the normal flow. Active timer, enabled pending IRQ,
delayed EI, HALT, disabled LCD, pending frame, different bytes and operands
outside FF80–FFFE remain in the interpreter. The limit of 16 iterations
per call is retained. No fixed Tetris address is used, and emulated speed
is not changed.

## Verification performed

- `tools/tests/picogb_idle_event_contract.sh`: 3,226 cases + NULL guards.
  Distances of 1–456 dots, PPU phases, VBlank/STAT, line 153, partial states,
  deferred flags, IRQ and subsequent instructions, ROM banks and rejections.
- Same suite with `SANITIZE=1`: passed with AddressSanitizer and UBSan.
- `tools/tests/picogb_idle_event_differential.sh`: compares execution
  without the fast path, 029 and 031. 600, 1,800 and 3,600 frames passed.
  The last includes movement, rotation, dropping and pause/resume through
  simulated input. Compares each frame: cycles, equivalent instruction
  count, pending IRQ, CPU, PPU/timer, WRAM/VRAM/OAM, HRAM/SRAM and
  framebuffer hashes.
- Results and logs: `tools/tests/picogb_build/idle_event/`.

| Frames | mb_exec calls in 029 | mb_exec calls in 031 | Fewer calls |
| --- | ---: | ---: | ---: |
| 600 | 1,379,510 | 890,682 | 488,828 |
| 1,800 | 4,384,016 | 2,995,471 | 1,388,545 |
| 3,600, simulated input | 7,744,545 | 4,851,680 | 2,892,865 |

These are host-side counts of avoided work, not a prediction of percentage
FPS gain on SH-2. `_main` code after LTO: 7,326 bytes, versus 7,228 in
029 (+98). These test counters and hashes are not included in the Saturn image.

## Build and package

Command: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_project.ps1 -Target mvpfastidleevent`

- `dist/kmvpe031.bin`: 604,160 bytes, MODE1/2048 data image.
- `dist/kmvpe031.cue`: references the correct BIN.
- Saturn header: T-99960G, V0.041, title knovGBC Idle Event 031.
- ISO opened with xorriso; 1ST_READ.BIN at LBA 33, 223,232 bytes,
  byte-identical to the newly compiled executable.
- Embedded ROM of 32,768 bytes: byte-identical to `roms/tetris.gb`.
- Baseline 029 was not rebuilt or replaced; its hash was preserved.

SHA256 kmvpe031.bin:
`BEBAE7CFEE1CB202B49CD0E55867C475E7798EE66499EDF8AD537C6F573E2249`

SHA256 kmvpe031.cue:
`777FC3E396F0ED65A7FB7394165D75E02D6D3CAA49BB241A6F427021579E7176`

SHA256 of preserved kmvpi029.bin:
`0BF5C91FC132AA58B24B03DF3DDFC80F93A7F6C95D19AEA17276311BF5E8D0EE`

Copy the BIN/CUE pair into a separate folder on Saroo and compare FPS/T/G/P
with 029 on the title screen and over a few minutes of gameplay. Check
controls, pause, collisions, line clearing and absence of freezes. Passing
on a PC does not replace console validation or certify all GB games.
