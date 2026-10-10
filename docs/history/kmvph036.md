# kmvph036 — continuous HRAM waiting

User-accepted Saturn reference: **kmvpd033**, approximately **40.6 logical
FPS / 20.5 visual FPS**, T0025 G0022 P0006. This candidate has not yet
been measured on the console and does not replace 033.

## Change

In `F0 a8 A7 28 FB` (`LDH A,(HRAM); AND A; JR Z`), 033 recognizes
the same ROM bytes again after each group limited by the next PPU event.
036 performs a full admission check once per entry into the wait loop
and advances successive groups without repeating that recognition. Each
group preserves Game Boy cycles, updates DIV and PPU, respects interrupt
ordering and returns at the end of a frame, before an enabled interrupt,
or after at most 512 groups. It does not retain recognition between entries
or hardcode a Tetris address. Active timer, disabled LCD and unsafe states
use the interpreter.

This is an isolated experiment based on 033: C with `-O3`, Master SH-2 +
VDP2, frameskip 1, unchanged controls and indicators, one embedded ROM,
no audio or saves. The 033 countdown fast paths remain.

## Internal verification

- Independent instruction-by-instruction contract
  (`picogb_hram_wait_contract.sh`): **7,036 cases + NULL guards**, with
  O3/LTO and ASan/UBSan. Includes F0, A7 and JR phases, flags, ROM/HRAM
  boundaries, PPU, VBlank, sprites, frameskip, interrupts and continuation
  in the handler that writes to HRAM.
- `picogb_hram_wait_differential.sh` comparison: **600, 1,800 and 3,600
  frames**, with simulated gameplay input. At each frame, cycles,
  equivalent instructions, state, memory and image match 033 and execution
  without fast paths. Final state at 3,600 frames: 26,972,874 equivalent
  instructions, 63,512,429 M-cycles, framebuffer `d90ed43cb756538f`,
  state `7d13978c673b8585`.
- Over 3,600 frames, 1,446,595 wait groups required 3,592 full admissions;
  **1,443,003 repeated admissions were avoided**. These are host-test
  counts, not FPS or time measured on Saturn.
- The first SH-2 compilation removed the routine through LTO
  specialization; the final version isolates only this function with
  `noipa`. The final map includes `picogb_hram_wait_try` at 0x06009768,
  with 0x6A4 bytes, and the executable was inspected to verify its presence.
  This does not predict the net FPS effect; the additional code also
  consumes space/cache.

## Saturn package

- `dist/kmvph036.bin`: 606,208 bytes; `dist/kmvph036.cue`: MODE1/2048,
  referencing the same BIN. Copy both together to Saroo.
- Header: T-99955G, V0.046, `knovGBC HRAM Wait 036`.
- `1ST_READ.BIN` at LBA 33: 225,280 bytes, byte-identical to the
  executable at `build/kmvph036/kmvph036.bin`.
- Embedded ROM of 32,768 bytes, byte-identical to `roms/tetris.gb`.
- The 033 BIN retains SHA256
  `EBE01DF09DDB6DC855996929623B3E9FCB57BAF6C7D04BAB0D130B1CE1E69EE8`.

SHA256 036 BIN:
`EDCD6E49BBE740BEE2F3387A9DC06299B734C7EB5C58CC1551B4364BA52A9A3E`

SHA256 036 CUE:
`FD0E1B20F1839AAB56B31C0E5F95C98FEF47D95834841D8DCE0180B8C0114D4A`

## Requested test

Compare FPS and T/G/P with 033 in the same scene after measurements stabilize,
then play for a few minutes, checking controls, pieces, score and lines.
If 036 loses performance or shows visual differences, continue with 033.
No higher FPS is assumed from the internal tests.
