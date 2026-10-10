# kmvpt035 — STAT waiting

Saturn reference: kmvpd033, 40.6 logical FPS / 20.5 visual FPS,
T0025 G0022 P0006. Experiment 034 recorded 40.2 / 20.1 and was discarded.
035 is a candidate: there is no console FPS measurement yet.

## Change

Recognizes only the sequence LDH A,(FF41); AND 03; JR NZ,-6
(bytes F0 41 E6 03 20 FA), in mapped ROM. Accepts STAT modes 1 through 3,
with the standard I/O callback and verified memory/PPU bindings.
Batches up to 16 iterations, preserves Game Boy cycles and stops at the
first instruction that reaches the next PPU event. At that point, the
frontend updates devices and delivers the interrupt before the next
instruction. Rejects active timer, disabled LCD, HALT, pending EI, enabled
pending IRQ and patterns/addresses outside the boundaries. Mode 0 follows
the normal path. It does not use a fixed address or depend on the ROM name.
033 fast paths remain; the 034 addition batching is not in this image.

The first SH-2 compilation removed the helper during LTO. The noipa
attribute retains runtime checks. In the final image, the helper is
456 bytes and is referenced by main; main measures 7,576 bytes versus
7,528 in 033. This confirms code presence, not an FPS gain.

## Checks

- O3/LTO and ASAN/UBSan contract: 17,379 STAT cases passed, covering
  CPU state, STAT, PPU phases, IRQ, ROM/banks and rejections.
- HRAM/resume fast-path regression: 10,206 cases passed in both
  test configurations.
- Per-frame comparison with the simple interpreter and 033: 600,
  1,800 and 3,600 frames identical in cycles, state, memory and image.
  The 3,600-frame scenario includes simulated controls.
- Over 3,600 frames, 43,296 interpreter calls were avoided through
  1,564 STAT groups. The pattern did not occur in the 600- and 1,800-frame
  scenarios. These numbers do not predict a proportional FPS gain.
- Final state at 3,600 frames: 26,972,874 equivalent instructions,
  63,512,429 M-cycles, framebuffer d90ed43cb756538f and state
  7d13978c673b8585, identical across all three variants.

## Package

Build: tools/build_project.ps1 -Target mvpfastidlestat.
dist/kmvpt035.bin: 604,160 bytes; CUE references it as MODE1/2048.
Header: T-99956G, V0.045, knovGBC STAT Wait 035.
1ST_READ.BIN at LBA 33 is 223,232 bytes and matches the compiled
executable byte for byte. The embedded 32,768-byte ROM matches
roms/tetris.gb byte for byte. 033 retains its original SHA256.

SHA256 BIN035: F40E786FA77A2A385725EB75379A261D036B4F360838C5A0DACDC2F6534E88A1
SHA256 CUE035: E8028C604358C944818EBC95DD06EF6BD05F0CE9E02397095ECB74CAB4060A52
SHA256 BIN033: EBE01DF09DDB6DC855996929623B3E9FCB57BAF6C7D04BAB0D130B1CE1E69EE8

## Saturn test

Copy BIN and CUE together into a separate folder on Saroo. Compare
against 033 in the same gameplay scene and report FPS, T, G and P
after a few minutes. Check movement, rotation, dropping, pause, lines
and stability. Adopt 035 as the reference only if it improves on the console.
