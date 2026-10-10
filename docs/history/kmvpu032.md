# kmvpu032 — resuming the fast path after events

User-approved Saturn reference: **kmvpe031**, 36.6 logical FPS /
18.3 visual FPS, T0027 G0024 P0006. That image was not rebuilt or replaced.
Subsequent Saturn result reported by the user: **39.9 logical FPS /
19.9 visual FPS, T0025 G0022 P0005**. 032 becomes the reference for
the next test; its original BIN remains preserved.

## Only functional change

The same `F0 a8 A7 28 FB` loop, reading zero from HRAM FF80–FFFE, can
enter the fast path at A7 and JR Z, in addition to the F0 position already
accepted by 031. Recognition uses ROM bytes and the current state, without
a fixed Tetris PC or recognition cache. A7 requires A=0; JR requires A=0,
F=A0 and FMC_NONE. Any incompatible condition returns to the interpreter
without changing state.

The initial suffix completes no later than the first instruction that
reaches the PPU event. If there is time before the event, complete iterations
of the same loop follow. The limit is the initial suffix plus 16 iterations.
PC, IR and flags are preserved at the exact stopping point. The frontend
commits DIV/PPU and delivers the interrupt before executing another instruction.

Retained: C core, -O3, Master SH-2 + VDP2, frameskip 1, controls and HUD,
one embedded ROM, no audio or saves. No additional wait pattern, Slave SH-2,
new assembly or graphics feature was introduced.

## Tests performed

- `tools/tests/picogb_idle_resume_contract.sh`: PASS, 10,206 cases + NULL.
  F0/A7/JR entries, distances of 1 to 480 dots and greater, PPU phases,
  VBlank, line 153, IRQ/IME, flags, ROM banks, changed HRAM, rejections and
  three instructions following the event. Oracle: instruction-by-instruction
  interpreter with real PPU/timer commits. State and framebuffer comparison.
- Same contract with `SANITIZE=1`: PASS with AddressSanitizer and UBSan.
- `tools/tests/picogb_idle_resume_differential.sh`: PASS, 600/1,800/3,600
  frames, comparing execution without the fast path, baseline031 and
  candidate032. Each frame: cycles, equivalent instructions, pending IRQ,
  CPU/device hashes, memory (including HRAM) and image. The 3,600-frame
  test includes movement, rotations, dropping and pause/resume through
  simulated input.
- Results in `tools/tests/picogb_build/idle_resume/`.

| Frames | mb_exec calls in 031 | Calls in 032 | Additional calls avoided |
| --- | ---: | ---: | ---: |
| 600 | 890,682 | 646,769 | 243,913 |
| 1,800 | 2,995,471 | 2,303,021 | 692,450 |
| 3,600 with input | 4,851,680 | 3,408,469 | 1,443,211 |

Over 1,800 frames, 296,761 resumes at A7 and 98,928 at JR were exercised.
These are counts of avoided work, NOT a proportional FPS prediction.
Test counters are not included in the Saturn image. `_main` after LTO grew
from 7,326 to 7,482 bytes (+156); the fast path remains integrated through LTO.

## Verified package

Build: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_project.ps1 -Target mvpfastidleresume`

- `dist/kmvpu032.bin`, 604,160 bytes; CUE correctly references that BIN.
- Saturn header T-99959G V0.042, title knovGBC Idle Resume 032.
- ISO opened with xorriso. `1ST_READ.BIN`, LBA 33, 223,232 bytes,
  byte-identical to the compiled executable.
- Embedded ROM of 32,768 bytes, byte-identical to `roms/tetris.gb`.
- Existing PicoGB/bare-metal linker compilation warnings; no errors.

SHA256 of candidate BIN:
`7551A4D11EBBA4CE38CD90B22F554B6649E5AC1906C88DB3EBD201070A608C01`

SHA256 of CUE:
`9B633E23A69C1707BE419100A83BC5AC933A1FBCD20D826A9B67A0BB093D605B`

SHA256 of preserved baseline031:
`BEBAE7CFEE1CB202B49CD0E55867C475E7798EE66499EDF8AD537C6F573E2249`

Next test: copy BIN/CUE together into a separate folder on Saroo, compare
FPS/T/G/P in the same scene as 031 and play for a few minutes. Check
controls, pause, collisions and line clearing. Host tests do not replace
the console or certify compatibility with all Game Boy games.
