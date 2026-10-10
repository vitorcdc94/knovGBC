# kmvpd033 — event-bounded countdown

Saturn reference: **kmvpu032**, 39.9 logical FPS / 19.9 visual FPS,
T0025 G0022 P0005, according to the user's reported test. The 032 BIN
was not rebuilt or replaced.

Subsequent Saturn result reported by the user: **40.6 logical FPS /
20.5 visual FPS, T0025 G0022 P0006**. 033 is the preserved reference
for the next candidate. That report did not specify the test duration.

## Only functional change

Fast path for `DEC A; JR NZ,-3` (bytes `3D 20 FD`), a finite countdown.
Recognition accepts mapped ROM or HRAM FF80–FFFE, without a fixed Tetris
address or dependence on the ROM name. In Tetris, this pattern appears
in the wait routine after the sprite-data transfer.

Batches up to 16 complete iterations and preserves emulated time. Stops
at the first instruction that reaches the next PPU event, or at the DEC
that produces zero; in the latter case, the final JR and next fetch remain
in the interpreter. A=0 is treated as a count of 256 decrements. Preserves
CPU state, flags and interrupt delivery at the same instruction boundary.
The frontend updates DIV, timer and PPU immediately after each group.

The fast path rejects active timer, disabled LCD, HALT, pending EI,
enabled pending interrupt, ready frame and sequences outside a safe region.
It revalidates all three bytes, including mutable HRAM, and the ROM bank
on each entry. It does not eliminate the DMA transfer or Game Boy wait
cycles. These conditions describe the current core configuration; they
do not certify compatibility with all games or future changes to the
DMA, timer or memory model.

Retained: 032 fast paths, C/-O3 core, Master SH-2 + VDP2, frameskip 1,
controls and HUD, one embedded ROM, no audio or saves.

## Internal evidence

- `tools/tests/picogb_idle_countdown_contract.sh`: PASS, **17,789 cases +
  NULL guards**, with -O3/LTO and AddressSanitizer/UBSan. The oracle uses
  the real instruction-by-instruction interpreter with device commits:
  all A values, flags, PPU events, VBlank/line 153, interrupts, memory
  boundaries, banks, HRAM mutation and subsequent instructions.
- `tools/tests/picogb_idle_countdown_differential.sh`: PASS, **600, 1,800
  and 3,600 frames**, comparing execution without fast paths, baseline032
  and candidate033. Per-frame comparison: cycles, equivalent instructions,
  IRQ, CPU/device/memory hashes and image. The 3,600 frames include
  simulated movement, rotation, dropping, pause and resume controls.
- Independent review of the helper, integration and tests: no blockers
  found for the current configuration.
- Results and logs: `tools/tests/picogb_build/idle_countdown/`.

| Frames | Interpreter calls in 032 | In 033 | Additional calls avoided | New groups |
| --- | ---: | ---: | ---: | ---: |
| 600 | 646,769 | 599,860 | 46,909 | 2,385 |
| 1,800 | 2,303,021 | 2,161,824 | 141,197 | 7,177 |
| 3,600 with controls | 3,408,469 | 3,125,785 | 282,684 | 14,374 |

This reduces remaining calls by approximately 6.1% in the 1,800-frame test
and 8.3% in the 3,600-frame test; it **does not mean a proportional FPS gain**.
Recognition costs and cache/layout effects must be measured on Saturn.
These internal counters were not added to the console build.

Final state at 3,600 frames: 26,972,874 equivalent instructions,
63,512,429 M-cycles; framebuffer FNV1a `d90ed43cb756538f`, state
`7d13978c673b8585`, identical across all three variants.

SH-2 executable inspection after LTO: separate **452-byte** helper, without
a test counter; `_main` 7,528 bytes, versus 7,482 in 032 (+46). The helper
is called only when the current opcode is DEC A. These sizes are not a
direct measure of cache occupancy or efficiency.

## Verified package

Build command:
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_project.ps1 -Target mvpfastidlecount`

- `dist/kmvpd033.bin`: 604,160 bytes. CUE uses MODE1/2048 and references this BIN.
- Saturn header: T-99958G V0.043, title `knovGBC Idle Countdown 033`.
- ISO inspected with xorriso: `1ST_READ.BIN`, LBA 33, 223,232 bytes,
  byte-identical to the compiled executable.
- Embedded ROM: 32,768 bytes, byte-identical to `roms/tetris.gb`.
- Compilation completed without errors. Existing qualifier, unused
  parameter/function and bare-metal linker RWX-segment warnings remain.

SHA256 BIN033:
`EBE01DF09DDB6DC855996929623B3E9FCB57BAF6C7D04BAB0D130B1CE1E69EE8`

SHA256 CUE033:
`7F0D7230C3F21E0CC188F4F1F9B067AD2EAC783A4A78E6FCD2C3ACD30B528B5C`

SHA256 of preserved baseline032:
`7551A4D11EBBA4CE38CD90B22F554B6649E5AC1906C88DB3EBD201070A608C01`

## Saturn test

Copy BIN and CUE together into a separate folder on Saroo. Compare FPS/T/G/P
against 032 in the same scene and play for a few minutes, checking controls,
pause, sprites, collisions and line clearing. Report title-screen and
gameplay readings. Adopt as the baseline only if console results justify
it; preserve 032 if the candidate does not improve.
