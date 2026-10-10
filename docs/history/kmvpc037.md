# kmvpc037 — CP n8 direct path

Saturn-approved reference: **kmvph036**, 43.5 logical FPS / 21.8
visual FPS, T0023 G0020 P0006, stable during gameplay according to the user's
test. 037 remains a candidate until measured on the console; 036 was preserved.

## Only core change

SM83 opcode `FE` (`CP n8`) now has a direct C path within the
interpreter, **after** interrupt priority handling and EI promotion.
The operand and next instruction still use the normal fetchers. A is not
modified. The path records the same deferred half-borrow (`FR1`, `FR2`,
`FMC_MODE=SUB_r8`), sets N/Z/C as before, clears H until its normal
materialization and returns the same 2 M-cycles. Other opcodes continue through
the previous interpreter. There is no new mapper, cache mechanism, Slave,
audio or save support.

037 retains everything else from 036: C with -O3/LTO, Master SH-2 + VDP2,
embedded Tetris, 160x144, frameskip 1 and continuous HRAM waiting.

## Internal checks

- Contract `tools/tests/picogb_cp_n8_contract.sh`: **262,144 arithmetic
  states** (all A/operand pairs, four previous flag states), comparing Z/N/C,
  deferred half-borrow, A, PC, next IR and 2 M-cycles. Also covers ROM0/ROMX
  and ROM/VRAM crossings, execution in SRAM, WRAM, OAM, HRAM and I/O,
  FFFF->0000 wraparound, priority IRQ and delayed EI.
  The reference and 037 passed O3/LTO; 037 passed ASan/UBSan.
- `tools/tests/picogb_cp_n8_differential.sh`: 600, 1,800 and 3,600 frames,
  including simulated gameplay controls. At every frame, cycles, state,
  memory and image are identical to 036. At the end of 3,600 frames:
  26,972,874 equivalent instructions, 63,512,429 M-cycles, framebuffer
  `d90ed43cb756538f`, state `7d13978c673b8585`.
- The SH-2 build enables the macro only in 037. After LTO, `main` grew from
  0x1B3C bytes (036) to 0x1C20 bytes (037), consistent with the new path being
  present, but this does not guarantee higher FPS: code size/cache effects
  can also make the candidate slower.

## Saturn package

- `dist/kmvpc037.bin`: 606,208 bytes, ISO9660 image.
- `dist/kmvpc037.cue`: MODE1/2048, referencing the same BIN.
- Header: `T-99954G`, `V0.047`, `knovGBC CP n8 Direct 037`.
- `1ST_READ.BIN` at LBA 33: 225,280 bytes, identical to the compiled executable.
- Embedded ROM: 32,768 bytes identical to `roms/tetris.gb`.
- SHA256 BIN 037:
  `DD05E68FC806C15866B92E42FB394407FE593299DBF64C3BE1C6747E9BF8D5E2`.
- SHA256 CUE 037:
  `F05205BCEA0F6F32E903706F060B772525DA5BD17F9AF9D4FD9218708679E786`.
- SHA256 BIN 036 remains:
  `EDCD6E49BBE740BEE2F3387A9DC06299B734C7EB5C58CC1551B4364BA52A9A3E`.

## Saturn test

Copy BIN and CUE together to Saroo. Compare 037 with 036 in the same scene
after the measurement stabilizes, then play for a few minutes to observe
pieces, controls, score and stability. If there is no real improvement or
there is a regression, keep 036 as the baseline. 038 is not included here.
