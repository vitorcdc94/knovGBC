# Publication preparation and verification

Record date: 2026-10-07. This is a source-release audit, not a hardware benchmark,
legal opinion, or declaration that the emulator is complete.

## Preservation and scope

- Project name remains **knovGBC**. No upload, root Git initialization, commit,
  remote mutation, or history rewriting is part of this preparation.
- Existing emulator C/header/assembly bodies and features are preserved.
  The optional Makefile ROM input override is build plumbing, not a core change.
- Existing ROMs, binaries, baseline builds, nested dependency Git histories and
  logs are retained locally but are not public source-release assets.
- Original README and available per-build validation reports were copied into
  `docs/history`. They are historical records, not current instructions.
- Selected licensed upstream source snapshots and original notices accompany
  the modified library; missing-authorization materials are withheld.
- Export-only LGPL inventory comments and the legacy launcher path replacement
  are recorded per file in `PUBLICATION_MANIFEST.json`.

## Verification results

The documented commands were exercised against a separate filtered source
copy, with export-only notices present. Environment: x86_64 WSL Ubuntu, host
GCC 15.2.0-16ubuntu1, installed big-endian SH-2 GCC 14.3.0/binutils 2.44 and
the existing local Yaul SDK. The space-free verification path and SDK path were
supplied through a local bind mount. This is not a fresh-machine install.

| Check | Result and scope |
| --- | --- |
| New PowerShell scripts parsed; Bash `build.sh` and suite syntax checked | Passed. Windows launcher also completed `-Check -Probe -ElfOnly` with the existing WSL/mount setup. |
| Original synthetic ROM fixture | Exactly 32768 bytes; zero logo region; valid header/global checksums. Invalid sizes, CGB/type/RAM classes rejected; separate 64 KiB MBC1 validation checked. |
| Preflight with existing SDK and deliberately absent SDK | Passed; absent prerequisites listed without writing files or claiming compilation. |
| `audio057_apu_test.sh` | Passed optimized and ASan/UBSan mono/stereo fallback parity against frozen 056. Exact PCM entries and state checks matched. |
| `picogb_056_saturn_audio_test.sh` | Passed SCSP mocks: initialization, backpressure, segment safety, silence/recovery and rollover. |
| `vdp2_zoom_test.sh` | Passed zoom/display/DMA mock checks, including indexed output. |
| `picogb_sm83_contract.sh` | Passed CPU/flag/opcode contract tests. |
| `picogb_ppu_regression.sh` | Passed synthetic PPU regression. |
| `SYNTHETIC_ONLY=1 picogb_055_tile_dirty_differential.sh` | Passed synthetic full-rate 054/055 comparison without a cartridge ROM. |
| `tools/build.sh --probe --elf-only` | Passed for latest 057 and explicit 055/056 profiles; cross-compiled/linked with the existing SDK and no IP-header generation. |
| `tools/build.sh --probe` | Passed private synthetic BIN/CUE packaging with the existing SDK. Generated artifacts are not source-release assets and are not included. |
| Source-copy audit | All 72 original `src` files retained; 25 library-extension comments preserved exact original code bodies. Main/vendor notices and fixture bodies retained. Manifest/export hash checks passed. |
| Existing baselines 050/055/056/057 | BIN SHA-256 values match the previously recorded values; 056/057 CUE values also match. No replacement game build was generated. |

The test suite can be run together with `bash tools/tests/publication_suite.sh`.
Separate private verification logs are outside the final source export. Compiler
warnings about unused declarations/parameters, return qualifiers and empty
assert bodies remain. The ELF linker also warns about a LOAD segment with RWX
permissions. These were documented, not suppressed or fixed by changing the
emulator during this handoff.

No new Saturn hardware test was performed. A synthetic build fixture does not
prove game booting, emulation accuracy, sound quality or real-time speed.

## Known build impediments

A fully fresh compiler/SDK bootstrap has not been verified. The public export
does not contain installed compilers, the unlicensed outer Yaul build-script
configuration, or Sega IP/security blobs. Full disc packaging needs an external,
lawfully configured Yaul SDK. Local testing with an existing SDK is narrower
than proving an independent clean install from this source archive alone.

Commercial ROM-dependent gameplay tests remain private and require user input.
Public host tests use synthetic inputs/mocks. CI configuration is supplied but
no GitHub-hosted execution has occurred as part of this preparation.

See [Building](BUILDING.md), [publication procedure](PUBLICATION.md),
[known emulator limitations](KNOWN_ISSUES.md) and [licenses](THIRD_PARTY.md).

## Source-copy language and publication review: 2026-10-10

This follow-up reviewed the user-selected source copy, not the private working
project. The sixteen reports under `docs/history` had already been translated.
The remaining Portuguese reports under `tools/tests` for builds 055, 056 and
057 were translated into English, preserving commands, identifiers, numerical
values, addresses and hashes. These remain historical reports: their statements
about pending hardware tests describe the date of each original report.
The README's outdated Portuguese-report note was corrected. Personal names,
proper nouns and program identifiers were not translated.

The copy at that point contained 1,192 files. File/path and text scans found no standalone
commercial ROMs, disc images, save files, PDF manuals,
Sega `sys_*.bin` blobs, nested `.git` directories, private configuration folders,
personal user-directory paths or recognizable credential tokens. Matches for
generic Windows paths in examples and credential-detection expressions in the
exporter are not credentials. Scans are heuristic, not a guarantee that every
possible secret or embedded copyrighted byte sequence has been identified.
That extension-based executable scan missed an extensionless ELF; the correction
and the superseding final inventory are documented below.

The only `.bin` files are the two previously reviewed SameBoy-source boot
outputs under `src/saturn/bootroms`; both hashes still match the reviewed values.
The retained `SameBoyLogo.png` is the licensed upstream build-source asset, not
a gameplay screenshot. Existing third-party licenses and copyright notices
remain in place; see `THIRD_PARTY.md` for redistribution scope and limitations.

The publication manifest's exported hashes, byte counts and transformation
notes were refreshed for changed documentation, including the earlier history
translations. Original-source hashes remain unchanged. No emulator source,
build logic, third-party source or license text was modified by this review.
No files were uploaded, and no new compilation or hardware test was performed
during that language-only review.

## Final corrective review: 2026-10-10

This section supersedes the earlier file count and executable-scan result.
Only the user-selected publication copy was corrected. The private emulator
project, its ROMs, baseline builds and existing nested Git histories were not
modified. Nothing was committed, pushed or uploaded.

- Removed the accidentally retained extensionless executable
  `third_party/SCSP_poneSound/PROJ/srcs` from this source copy. It is a 19,004-byte
  ELF32 big-endian Motorola 68000 binary, SHA-256
  `98B7221049ECDE5700278BA0367A1073EA955B4CFEF447878C2D23A64F5785DF`.
  The original is untouched and the removed copy has a recoverable private backup.
- Restored fourteen tracked libYaul build inputs byte-for-byte from the private
  dependency snapshot: eleven Makefile templates, two C++ `.cxx` files and
  `libyaul/common/wrap-error`. These are source inputs, not generated output.
- Corrected the exporter and `.gitignore` with narrow exceptions for these
  inputs. The exporter now checks compiled-file signatures before accepting
  extensions or special names; generated outputs and Sega security blobs remain
  excluded. Ninety-one isolated exporter assertions passed, including renamed
  ELF/PE files, source-template exceptions and repeat-export transformations.
- Corrected the README's performance description: reported near-full-speed
  Tetris operation applies to the no-audio baseline, not the approximately
  24.5 logical FPS audio build. Removed five upstream gameplay screenshot embeds
  from the filtered Peanut-GB README; the original notices are retained.
- Added actual 2026-10-10 dates to the forty-one publication notice comments and
  their change inventory. Their emulator/fixture source bodies still match the
  private originals byte-for-byte. Dated historical reports are evidence of
  milestones, not invented dates for every older source edit.
- Added `tools/verify_publication.ps1`, a read-only inventory, known-private-marker
  and binary-signature check. A fresh round-trip export retains the recovered
  inputs. The final copy contains 1,206 files: 1,205 manifested source/input
  files and the manifest itself. Original-source hashes are preserved; changed
  exported hashes and correction notes are refreshed.

The final checker passed for all 1,205 manifest entries. A separate re-export
passed the same check and reproduced all 1,205 payload files byte-for-byte.
Isolated negative tests correctly rejected a renamed ELF, an unexpected `.gb`
file containing only license text (not a game), changed manifested content and
a missing manifested input; the restored clean fixture passed. All seventy-two
private-original `src` file bodies and ten matched license texts remain intact.
An independent read-only documentation review found no actionable contradiction
and all twenty-nine checked local Markdown link occurrences resolve.

The six host test groups in `tools/tests/publication_suite.sh` passed again in
a temporary WSL Ubuntu copy. The 055, 056 and 057 profiles also compiled and
linked with `--probe --elf-only`, the original synthetic ROM fixture and the
existing local SH-2 GCC 14.3.0/Yaul SDK. No new commercial-game disc image was
generated. Existing compiler warnings and the ELF RWX LOAD-segment warning
remain documented. A fresh SDK bootstrap, GitHub CI run and Saturn hardware
test were not performed and are not claimed.

Two licensing limitations remain explicitly disclosed rather than certified
away. The two libYaul PRS files report free use with attribution in their source
headers, but no primary formal grant was independently established; they must
not be treated as automatically MIT/zlib-licensed. Exact historical modification
dates for some LGPL-derived files cannot be reconstructed from the available
records. Today's dated notice edits do not certify compliance with the older
modification-date requirement. See `THIRD_PARTY.md` and
`third_party/PicoGB/KNOVGBC_CHANGES.md` for the evidence and limits.

The publication checker does not establish ownership, provide a legal opinion,
or guarantee absence of every secret or copyrighted byte sequence. Keep all
included license and attribution files. Do not publish commercial ROMs, old
game BIN/CUE releases, Sega security blobs or private staging/backup folders.
