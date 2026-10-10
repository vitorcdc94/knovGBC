# Audio build 056 validation

Completed on 2026-10-04. Saturn test candidate; sound quality,
perceived synchronization and hardware performance not yet validated.

## Scope and architecture

`mvpfastzoomaudio` / `kmvpa056` preserves the features of 055: Tetris DMG,
Master SH-2, C interpreter with `-O3`/LTO, indexed8/DMA VDP2 zoom, three-digit
HUD, rendering without frameskip and selective tile invalidation.
It does not add saves, a ROM menu, control selection or Slave SH-2 usage.

The Master synthesizes the four DMG channels into signed16 mono PCM at 32768 Hz.
The output uses box integration and a fixed-point high-pass filter.
The SCSP plays this PCM in slot 0; the 68K receives only a 14-byte idle loop
and is restarted immediately after it is loaded. No recorded music,
game-clock acceleration or time stretching is used.

The previous APU core was not enabled in recent targets. This integration
replaced per-cycle advancement with event counters and corrected NR52,
routing, length, envelope, sweep, wave and noise.
I/O retains the instruction-boundary precision of the current interpreter;
this is not an analog-accurate or bus-cycle-perfect APU.
Model-specific envelope glitches and analog DAC discharge are not modeled.

References used: [Pan Docs, Audio Details](https://github.com/gbdev/pandocs/blob/master/src/Audio_details.md),
[Audio Registers](https://github.com/gbdev/pandocs/blob/master/src/Audio_Registers.md),
SCSP manual `ST-077-R2-052594.pdf` and Technical Bulletin 51 supplied by
the user. Initialization order respects shared SMPC usage:
profiler/FRT, sound setup, then asynchronous controller INTBACK.

## Buffers and test limitations

- 4096-sample / 8 KiB ring in main RAM, with a monotonic cursor.
- 8192-sample / 16 KiB SCSP ring in sound RAM, starting at 0x10000.
- The CA monitor has 4096-sample page granularity. Writes do not modify the
  currently playing page; consumed pages are cleared.
- Playback starts after 4096 samples, approximately 125 ms. This is a conservative
  choice for the first integration, not a final low-latency design.
- Pitch `0x79F2`: approximate SCSP rate of 32773.535 Hz (+0.017% relative
  to the producer). This small difference may cause drift over long sessions.
- When samples are missing, output is silence rather than repeated old data.
  Emulation below real-time speed may produce gaps/clicks. Service intervals
  of 100 ms or more rearm only the PCM slot.
- Backpressure retains samples in main RAM. If that ring also fills up,
  the oldest samples are discarded and counted; the game is not blocked.
- Internal statuses `queued_samples`, `underrun_count`, `recovery_count` and
  `core.audio_dropped` are available for diagnosis without additional HUD cost.

## Checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_project.ps1 -Target audiotest
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_project.ps1 -Target mvpfastzoomaudio
```

- APU: exact sample counts, silence when powered off, non-power-of-two and
  mask-based capacities, cursor wrap, register masks, DIV, length, envelope,
  sweep/overflow, pulse, wave RAM, noise, volume/pan and invariance when
  dividing the same elapsed time across different calls.
- SCSP backend with mocked MMIO: 68K vectors/stub, SNDON/SNDOFF sequence,
  IRQ mask, all 32 slots disabled, safe buffers, negative PCM,
  backpressure, starvation, recovery, wrap and PAL.
- The above tests also ran with AddressSanitizer and UBSan, without errors.
- Tetris: 3600 frames with automated menu, movement, rotation, drop and pause
  inputs. CPU, cycles, interrupts, memory and images for each frame matched
  the audio-disabled 055 configuration. O3/LTO and sanitized execution
  produced identical results.
- 1,984,763 samples, exactly `floor(ciclos_emulados / 32)`. 1,951,070
  nonzero samples, minimum -26188, maximum 25276; PCM FNV-1a hash
  `206626b0920e59da`. This verifies generation and timing, not listening quality.
- All active paths advance the APU exactly once: normal CPU execution,
  HALT, countdown and HRAM wait runner. Shortcuts do not remove audio time.
- SH-2 build completed. Existing upstream parameter/qualifier warnings
  and the linker's existing RWX segment warning remain.
- Package: Saturn signature, JUE regions, ISO9660 and 2048-byte sectors,
  relative CUE and `1ST_READ.BIN` as the first file. The program in the ISO
  matches the executable, and the embedded ROM matches the source Tetris ROM.
- `.bss`: 122144 bytes; static end `0x0603C598`, Master stack
  `0x060FFFF0`. No static overlap; this does not measure actual peak stack usage.

## Preserved artifacts

Delivery: `dist/dist Tetris/kmvpa056.bin` (612352 bytes) and `.cue` (71 bytes).
Executable: 231424 bytes. SHA-256 of the delivered BIN:
`A1B46D9BBBF1A862291FC5705DE3AB7CD17AD985700A19096B688AD666EBAC8C`.

Earlier BINs were not rebuilt or overwritten; hashes checked:

- 055: `6E13C2F0F0F2C23896B5F687ED6B54C35C4E3E51E375A391025C1A9DE882DC03`.
- 054: `9F0A404F9C9123269A30E83233B22DE4738BD6A3EF2519AA4AC121BCC800091D`.
- 053: `58643411BC04D1121FD28E53E2CFDAE92C0227B2526E951E0A069E7F2A5CF5C2`.

## Next hardware test

Keep BIN/CUE together. Check boot, controls, music A/B/C and OFF,
piece effects, pause/resume and several minutes of gameplay. Report
FPS/T/G/P and any silence, clicks, distortion or noticeable delay.
The host test does not simulate the SCSP or bus contention;
sound and speed can only be considered validated after this hardware test.
