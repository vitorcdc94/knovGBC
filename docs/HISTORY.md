# Development history and retained experiments

This summary uses the conversation and surviving source/reports, not a
reconstructed Git log. No project-root Git history existed. Build numbers
identify experiments, not semantic versions.

Initial work ported SameBoy with a Saturn/Yaul frontend and established
loader/startup, vectors, stacks, input and display. The narrow DMG MVP then
switched to PicoGB while preserving the Saturn shell.

Experiments included memory/HRAM fast paths, opcodes, compiler/cache layouts,
assembly, guarded loop/wait shortcuts, PPU batching/dirty tracking, VDP2 DMA,
indexed output, nonblocking input and audio. Unsuccessful sources are retained.

| Milestone | Significance / reported outcome |
| --- | --- |
| Early SameBoy / MVP | Historical path, far below real-time target |
| Initial PicoGB, memory/PPU work | Tetris boots and controls work; roughly 14-16 logical FPS |
| Assembly/compact CPU / Slave | Regressions; not promoted as recent baselines |
| Guarded waits, 029 onward | Larger speed gains with device-time/state comparisons |
| 036 HRAM | About 43.5 logical FPS reported |
| 041 adaptive PPU / 042-044 | Around 47.9 logical FPS; fixed-group trials not a large extra gain |
| 045 direct DMA | Around 53.4 logical FPS; presentation intermediate copy removed |
| 046 input / 047 indexed8 | Lower presentation cost, but some gameplay still lagged |
| 048-050 dirty work | Tetris reached near real time; 049 VDP1/VDP2 regressed |
| 051 | Separate SML experiment, around 20.5 logical FPS |
| 052-053 | Aspect-preserving zoom, side HUD, three-digit timing |
| 054-055 | All frames displayed; selective tile invalidation; roughly 53-59 FPS |
| 056 | Experimental sound; about 24.5 FPS, T042/G040/P001 |
| 057 | Cached equivalent mono mixer; host parity, no visible hardware gain reported |

The developer ended work and requested a public source handoff in October
2026. No subsequent audio scheduler optimization was implemented.

## Retained evidence

- [Initial README](history/README.initial.md) is deliberately obsolete;
  current build instructions are elsewhere.
- `docs/history/` copies validation Markdown from private build directories.
  Originals remain intact locally; games/executables/disassemblies/screenshots
  are not distributed with the reports.
- `tools/tests/picogb_055_validation.md`, `picogb_056_validation.md` and
  `picogb_057_validation.md` document their stages' tests and limitations.
- Existing C/assembly and experiment modes are retained. Publication work
  adds documentation/build infrastructure, not an emulator optimization.
- Upstream base revisions are in `docs/THIRD_PARTY.md`; they do not replace
  locally modified snapshots.
