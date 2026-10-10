# knovGBC PicoGB change record

Inventory date: 2026-10-07. Source-handoff documentation and all 41 per-file
modification notices revised on 2026-10-10. The latter is the actual date of
this documentation/notice change; the preserved source bodies were not edited.
The local implementation changes predate the inventory. Their exact historical
code-edit dates are not established by the surviving records. Neither date is
a claim that all of those implementation changes occurred on the same day.

Upstream project: [SonoSooS/PicoGB](https://github.com/SonoSooS/PicoGB).
Upstream base: [325a12a25e2d1f4799424cc18c0e69a7810695b0](https://github.com/SonoSooS/PicoGB/tree/325a12a25e2d1f4799424cc18c0e69a7810695b0).
PicoGB is licensed under [GNU LGPL v2.1](LICENSE.txt), as stated in its upstream
README. The modified library keeps that license. This record does not replace
the license, remove an upstream notice, or relabel PicoGB as MIT.

## Modified upstream files

The local checkout differs from the recorded upstream base in these twelve
files. The summaries describe the source changes, not a guarantee that every
experimental build is enabled or tested in every hardware configuration.

| File | Local changes |
| --- | --- |
| `apu.c` | DMG audio countdown/mixing implementation, register and waveform timing, sample integration/filtering, ring output, and optional cached mono mixing. |
| `apu.h` | Audio channel/mixer state, flush/reset interfaces, ring cursor and sample state, and optional mono coefficients. |
| `config.h` | Documents the local Saturn RGB1555 PPU format. |
| `fabric.c` | Table-driven Game Boy timer periods, APU DIV reset integration, video-write notifications, and PPU palette/dirty-state invalidation. |
| `fabric.h` | Exposes the timer-period table used by the local timing paths. |
| `mi.h` | Adds optional VRAM/OAM write-notification callbacks for derived rendering/shadow state. |
| `microcode.c` | Configurable Saturn interpreter fast paths, opcode/stack profiling hooks, cold-handler layout, compact/block-executor hooks, and guarded direct operations. |
| `microcode.h` | Correct big-endian register layout and endian-safe dynamic byte-register access. |
| `microcode_dispatch.c` | Configurable flat-ROM/direct memory paths, stack and operand access optimizations, and video-write notifications. |
| `ppu.c` | Native Saturn palette backends, palette/tile/sprite caching, dirty scanline/tile tracking, PPU event batching, and capture/validation hooks. |
| `ppu.h` | Optional renderer configuration, derived cache/latch/dirty state, and local PPU interfaces. |
| `types.h` | Host/Saturn byte-order handling and configuration for native 16-bit and indexed 8-bit pixels. |

The local library additionally has two palette implementations absent from
that upstream base:

- `ppu/ppu.RGB1555.c`: four native Saturn RGB1555 shades for the DMG renderer.
- `ppu/ppu.VDP2_INDEX8.c`: four final DMG shade indices for VDP2 palette output.

These files form part of the modified PicoGB library and are included under
the same GNU LGPL v2.1 terms.

## Dated evidence and historical date limits

The retained English reports identify dated development/validation milestones.
They show that the reported features existed at those milestones, but do not
record the exact date of every edit or establish that every part of the final
preserved source was written on a report date.

| Source files / reported feature | Retained evidence | Date and meaning |
| --- | --- | --- |
| `third_party/PicoGB/ppu.c`, `ppu.h`: selective tile/scanline invalidation | [055 validation](../../tools/tests/picogb_055_validation.md) | Completed on 2026-10-03; validation of the tile-dirty feature, not the dates of all PPU edits. |
| `third_party/PicoGB/apu.c`, `apu.h`: event-counter APU and optional cached mono mixer | [056 validation](../../tools/tests/picogb_056_validation.md), [057 validation](../../tools/tests/picogb_057_validation.md) | Both completed on 2026-10-04; validation of these APU stages, not the dates of all APU edits. |
| `tools/tests/fixtures/audio056/apu.c`, `apu.h`: earlier APU snapshot | [057 validation](../../tools/tests/picogb_057_validation.md) | Completed on 2026-10-04 and explicitly identifies these immutable fixtures as code from before the mono-mixer change; exact original-edit and snapshot-copy dates are unknown. |
| `src/saturn/picogb_hram_wait.c`: adaptive and fixed-group HRAM/PPU batching | [041 report](../../docs/history/kmvpe041.md), [042/043 comparison](../../docs/history/PPU_FIXED_COMPARISON_042_043.md) | Reports dated 2026-09-25 explicitly identify the changed preprocessed HRAM wait module; exact edit dates are unknown. |

Abbreviated `apu.h` and `ppu.h` paths in the table share the preceding directory.
The [031 report](../../docs/history/kmvpe031.md) additionally records generation
on 2026-09-22 for an earlier event-boundary wait experiment. It does not supply
an exact per-file edit date and is not assigned as the modification date of all
timing helpers. The remaining inventoried files have no verified exact local
code-edit dates in the retained reports; their notices explicitly preserve that
uncertainty and refer to this record.

There was no project-root Git history. The surviving PicoGB Git revision is
the upstream base, with the local changes uncommitted; it cannot supply their
historical dates. Filesystem timestamps provide local context but are not an
edit history and are not asserted here as original modification dates.

The 2026-10-10 correction records a real notice/documentation change and keeps
the historical limits visible. It does not certify that the unresolved
historical modification dates satisfy LGPL v2.1 section 2(b). Further dated
original records would be needed to resolve that gap; do not replace unknown
dates with the inventory date, notice date, or validation completion dates.

## Local implementation scope outside this directory

The original project-wide MIT notice does not supersede PicoGB-derived code
stored under `src/saturn`. A conservative public-export scope for the library's
local execution/rendering/timing extensions is:

| Area | Files relative to the repository root |
| --- | --- |
| Frontend execution integration | `src/saturn/picogb_mvp_main.c` |
| Compact interpreter and SH-2 execution | `src/saturn/picogb_sm83_compact.c`, `picogb_sm83_compact.h`, `picogb_sm83_exec_sh2.sx`, `picogb_sm83_sh2.c`, `picogb_sm83_sh2.h` |
| Block execution | `src/saturn/picogb_block_cache.c`, `picogb_block_cache.h`, `picogb_block_executor.c`, `picogb_block_executor.h` |
| Device/event and guarded-loop execution | `src/saturn/picogb_event_scheduler.h`, `picogb_hram_wait.c`, `picogb_hram_wait.h`, `picogb_idle_spin.c`, `picogb_idle_spin.h`, `picogb_idle_countdown.c`, `picogb_idle_stat.c`, `picogb_sum_loop.c` |
| PPU-derived capture/shadow implementation | `src/saturn/picogb_slave_ppu_shadow.c`, `picogb_slave_ppu_shadow.h`, `picogb_vdp12_frame.c`, `picogb_vdp12_frame.h` |
| PPU copy helpers | `src/saturn/picogb_ppu_tile_copy.c`, `picogb_ppu_tile_copy.h`, `picogb_ppu_tile_copy_sh2.sx` |

All abbreviated filenames in a row share the `src/saturn/` prefix. For source
publication, retain GNU LGPL v2.1 treatment for PicoGB-derived portions of
these implementations and their interfaces. The compact/block CPU paths and
SH-2 execution translation mirror the library's internal interpreter contract,
lazy half-carry state, prefetch, and interrupt/HALT behavior. The timing and
rendering modules directly extend its internal execution/state machinery.
The frontend integrates those paths and follows the upstream initialization
and execution model in `test/winmain.c` and `pgb_main.h`.
Grouping the associated helpers and interfaces with the library is a
conservative publication approach, not a legal finding that every short
declaration or independently written hardware algorithm is copyrighted
derivative expression.

Independently written Saturn input, SCSP output, platform, and profiling code
can keep the original MIT grant. Merely calling PicoGB, including its API
headers, or using a numeric parameter/data layout does not automatically
change a file's license. Any further copied or translated PicoGB implementation
must preserve the LGPL regardless of its directory or filename.

## Per-file modification notices for an export

The inventory alone is not a substitute for LGPL v2.1 section 2(b)'s prominent
notice on each modified file. This prepared export contains 41 C-style
modification notices: twelve modified upstream files, two palette additions,
25 local execution/rendering/timing extensions, and two frozen APU fixtures.
Only those comments were revised on 2026-10-10. Removing each added comment
leaves the source body byte-identical to the preserved local original.
The notices date this actual source-handoff change and explicitly distinguish
it from the unknown historical implementation-edit dates.

Suggested comment for each of the twelve modified upstream files:

```c
/* knovGBC modification notice updated 2026-10-10.
 * This file differs from upstream PicoGB commit
 * 325a12a25e2d1f4799424cc18c0e69a7810695b0.
 * The preserved local code changes predate the 2026-10-07 inventory.
 * Exact historical code-edit dates are not established by surviving records.
 * Only this source-handoff notice was changed on 2026-10-10.
 * See third_party/PicoGB/KNOVGBC_CHANGES.md for dated validation evidence;
 * report dates are not claims that every code change occurred on those dates.
 * PicoGB and this modified library remain under GNU LGPL v2.1.
 * This notice does not replace upstream license or copyright notices.
 */
```

Suggested comment for local library additions/extensions:

```c
/* knovGBC PicoGB library extension/snapshot; notice updated 2026-10-10.
 * This source extends/adapts the PicoGB library based on upstream commit
 * 325a12a25e2d1f4799424cc18c0e69a7810695b0.
 * The preserved local implementation predates the 2026-10-07 inventory.
 * Exact historical code-edit dates are not established by surviving records.
 * Only this source-handoff notice was changed on 2026-10-10.
 * See third_party/PicoGB/KNOVGBC_CHANGES.md for dated validation evidence;
 * report dates are not claims that every code change occurred on those dates.
 * Distributed with the modified library under GNU LGPL v2.1.
 * See third_party/PicoGB/LICENSE.txt for the library license.
 * Original upstream license and copyright notices remain applicable.
 */
```

Document/license paths in the notices are repository-relative. APU, tile-dirty,
HRAM batching and fixture notices additionally point to the file-specific dated
evidence above. The frozen APU notices describe their earlier-snapshot role.
The original private C/header/assembly files were not edited by this task;
the comments describe source-export preparation. Independently written files
should not receive a false claim that their code was copied upstream.

## Release materials

The frozen `tools/tests/fixtures/audio056/apu.c` and `apu.h` are copies of an
earlier modified APU implementation and keep PicoGB-derived LGPL v2.1 terms.
Their regression-snapshot role does not change their license. The source
export retains a corresponding source-handoff notice on those copies, revised
on 2026-10-10, preserving their original bodies and accompanying library license.

Keep `LICENSE.txt`, this change record, the modified sources and local library
extensions together. For statically linked binary releases, also provide a
prominent library-use notice and the corresponding application/source or
object and build materials needed to modify PicoGB and relink. Observe LGPL
v2.1 sections 2, 5, and 6; see the [GNU linking FAQ](https://www.gnu.org/licenses/gpl-faq.en.html#LGPLStaticVsDynamic).

The source inventory does not clear commercial ROMs, Saturn security/header
blobs, or other third-party media. Those rights are separate from PicoGB's
software license. See [the repository-wide inventory](../../docs/THIRD_PARTY.md).
