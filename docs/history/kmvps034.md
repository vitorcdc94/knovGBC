# kmvps034 — addition with a counter

Preserved reference: **kmvpd033**, user-reported result:
40.6 logical FPS / 20.5 visual FPS, T0025 G0022 P0006. Its BIN was
not rebuilt or replaced. The subsequent 034 Saturn test recorded
40.2 logical FPS / 20.1 visual FPS, T0025 G0022 P0006. There was no
measurable gain; 033 remains the reference.

## Only new optimization

Recognizes exactly `ADD HL,DE; DEC B; JR NZ,-4` (bytes `19 05 20 FC`)
in mapped ROM or HRAM. It does not use a fixed address, ROM name or
Tetris-specific condition. It does not include STAT, LY, memory copies
or other patterns. Retains the 033 fast paths and their original conditions.

The addition is calculated in a batch without eliminating the game's
logical work. An iteration with a taken branch costs 6 M-cycles / 24 dots
(ADD 2, DEC 1, JR 3 M-cycles). The helper stops at the first instruction
that reaches the next PPU event, or at the DEC that makes B zero; the
terminal JR and its following fetch remain in the normal interpreter.
The limit is 16 taken-branch iterations per group. B=0 represents 256
decrements, and results respect 16-bit wraparound.

Preserves the last addition's carry, flags/deferred mode and FR1/FR2,
including stops after ADD and DEC. Updates DIV/timer/PPU after each group
and delivers IRQ before the next instruction, as the reference does.
Active timer, disabled LCD, pending EI, HALT, enabled pending IRQ and
unrecognized conditions return to the interpreter without changing state.

All four bytes must lie entirely within an allowed region. Bank and
mutable HRAM are revalidated at each entry. No code cache is added.
These guarantees apply to the current core model; they do not certify
all games or future changes to DMA, timers or memory.

Retained: C/-O3, Master SH-2 + VDP2, frameskip 1, HUD/controls, one embedded
ROM, no audio, saves, Slave SH-2 or new handwritten assembly.

## Tests

- `tools/tests/picogb_sum_loop_contract.sh`: **67,006 cases + NULL guards**
  passed with -O3/LTO and AddressSanitizer/UBSan. Includes all B values,
  HL/DE pairs with carry/half-carry and wraparound, flags, PPU phases,
  VBlank/line 153, ROM/HRAM boundaries, bank/code changes and rejections.
- Independent oracle: instruction-by-instruction `mb_exec`, with device
  commits, comparing cycles, PC/IR, registers, FR1/FR2/FMC_MODE, devices,
  memory and image; also four subsequent instructions and interrupts.
- `tools/tests/picogb_sum_loop_differential.sh`: PASS across **600, 1,800
  and 3,600 frames**, comparing execution without batching, baseline033
  and candidate034. Cycles, equivalent instructions, IRQ and state/memory/
  image hashes match at every frame. The 3,600-frame scenario includes
  simulated controls.
- Independent static review of the helper and integration: no functional
  divergence found.
- Outputs: `tools/tests/picogb_build/sum_loop/`, including
  `contract_o3.result.txt` and `contract_sanitize.result.txt`.

| Scenario | Interpreter calls in 033 | In 034 | Additional calls batched | New groups |
| --- | ---: | ---: | ---: | ---: |
| 600 frames | 599,860 | 599,860 | 0 | 0 |
| 1,800 frames | 2,161,824 | 2,161,824 | 0 | 0 |
| 3,600 with controls | 3,125,785 | 3,084,363 | 41,422 | 2,665 |

The pattern did not occur in the first two scenarios. Therefore, no batching
gain is expected on the title screen; testing must include a game session.
In the gameplay scenario, the call reduction is approximately 1.33%,
**not a proportional FPS prediction**. Recognition costs and cache/layout
effects exist; the net gain may be small, zero or negative.

Final state at 3,600 frames: 26,972,874 equivalent instructions,
63,512,429 M-cycles; framebuffer FNV1a `d90ed43cb756538f`, state
`7d13978c673b8585`, identical across all three variants.

SH-2 after LTO: separate 576-byte helper; `_main` 7,572 bytes, versus
7,528 in 033 (+44). Multiplications/constant division use SH-2 operations,
without an external division-routine call in the helper. New counters
are host-test-only and are not included in the console image.

## Package

Build:
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_project.ps1 -Target mvpfastsum`

- `dist/kmvps034.bin`: 604,160 bytes; MODE1/2048 CUE references this BIN.
- Header: T-99957G V0.044, title `knovGBC Sum Loop 034`.
- ISO inspected with xorriso: `1ST_READ.BIN` at LBA 33, 223,232 bytes,
  byte-identical to the compiled executable.
- Embedded ROM of 32,768 bytes, byte-identical to `roms/tetris.gb`.
- Compilation without errors; existing PicoGB and bare-metal linker
  warnings (RWX segment) remain.

SHA256 BIN034:
`7D5FBFF4DE7391B0FA1E5BAEC1FAD6D16B3D2E8C1D1CE2EC20273F4AA6E0958D`

SHA256 CUE034:
`89816F83472862BF455FA157BE07969B0A06E5E4E0A13A7AA633DC974D396DC8`

SHA256 of preserved baseline033:
`EBE01DF09DDB6DC855996929623B3E9FCB57BAF6C7D04BAB0D130B1CE1E69EE8`

## Next Saturn test

Copy BIN/CUE together into a separate folder on Saroo. Compare against 033
during equivalent gameplay, including movement, rotation, dropping,
pause/resume and line clearing. Observe FPS/T/G/P and stability over a few
minutes. The title screen serves to detect regressions, not to measure
this pattern's benefit. Do not replace the baseline before the result.
