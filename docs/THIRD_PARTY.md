# Third-party software and publication scope

Inventory recorded on 2026-10-07. Paths below are relative to this repository.
PRS exception and source-input publication notes updated on 2026-10-10.
This document records the licenses and provenance observed in the local source
trees. It is not a legal opinion or a certification that every asset is cleared
for redistribution.

The root [MIT license](../LICENSE) applies to original, independently authored
knovGBC code. It does not replace third-party licenses or authorize distribution
of third-party game data, firmware, fonts, images, or downloaded executables.
Keep the upstream license files and file-specific notices with any published
copies of their covered sources. A dependency need not be linked into the
emulator for its own source distribution to require its notices.

## Source provenance

These commits identify the upstream bases of the local dependency checkouts.
The local SameBoy, libYaul, and PicoGB trees have modifications; a commit URL
alone does not contain those modifications or substitute for their source.

| Component | Upstream base | Observed license and notices |
| --- | --- | --- |
| SameBoy | [LIJI32/SameBoy, 213a12ce93d66b105a113debd9396306066a7cfc](https://github.com/LIJI32/SameBoy/tree/213a12ce93d66b105a113debd9396306066a7cfc) | [Expat/MIT](../third_party/SameBoy/LICENSE), copyright 2015-2026 Lior Halphon; exceptions described below. |
| libYaul | [yaul-org/libyaul, 6012f79f237773378c8014e70d8998ad95a38d98](https://github.com/yaul-org/libyaul/tree/6012f79f237773378c8014e70d8998ad95a38d98) | [MIT](../third_party/libyaul/LICENSE), copyright 2012-2023 Israel Jacquez; file-specific exceptions and unresolved PRS permission provenance described below. |
| PicoGB | [SonoSooS/PicoGB, 325a12a25e2d1f4799424cc18c0e69a7810695b0](https://github.com/SonoSooS/PicoGB/tree/325a12a25e2d1f4799424cc18c0e69a7810695b0) | [GNU LGPL v2.1](../third_party/PicoGB/LICENSE.txt), as identified by upstream README. See [the local change inventory](../third_party/PicoGB/KNOVGBC_CHANGES.md). |
| Peanut-GB | [deltabeard/peanut-gb, 8e656982f08663785794b84823d3e27f856fdb7f](https://github.com/deltabeard/peanut-gb/tree/8e656982f08663785794b84823d3e27f856fdb7f) | MIT notice in `peanut_gb.h`, copyright 2018-2023 Mahyar Koshkouei, including attribution to SameBoy code, copyright 2015-2019 Lior Halphon. |
| gnuboy | [rofl0r/gnuboy, c367bb4ba96fb07cd62f72f5ecb43aeff7012564](https://github.com/rofl0r/gnuboy/tree/c367bb4ba96fb07cd62f72f5ecb43aeff7012564) | [GNU GPL v2](../third_party/gnuboy/COPYING); `main.c` grants version 2 or any later version and credits Laguna and Gilgamesh, 2000-2001. |
| SCSP_poneSound | [ponut64/SCSP_poneSound, c2b0d3a929d6f4f124fccad5f1e0359f8862c7e2](https://github.com/ponut64/SCSP_poneSound/tree/c2b0d3a929d6f4f124fccad5f1e0359f8862c7e2) | [MIT](../third_party/SCSP_poneSound/LICENSE), copyright 2020 ponut64. |
| crosstool-NG 1.28.0 | [crosstool-ng/crosstool-ng release 1.28.0](https://github.com/crosstool-ng/crosstool-ng/releases/tag/crosstool-ng-1.28.0) | GPL v2 by default, documentation CC BY-SA 2.5, patches under the licenses of the software they modify, and individual-file exceptions. No Git commit metadata was present for the extracted release directory. |
| RGBDS 1.0.3 | [gbdev/rgbds release v1.0.3](https://github.com/gbdev/rgbds/releases/tag/v1.0.3) | Upstream [MIT license](https://github.com/gbdev/rgbds/blob/v1.0.3/LICENSE). The locally extracted executable archive did not include a license file; the tools are not part of the proposed public source export. |

Two additional local checkouts have no global license identified and are
withheld from the public export:

- [tpunix/SAROO, c31bf6192eff59533d7268dfdf6c791f11a1f7c9](https://github.com/tpunix/SAROO/tree/c31bf6192eff59533d7268dfdf6c791f11a1f7c9).
- [ijacquez/libyaul-build-scripts, 10cc8fbe414c54c87f9ad7bc50c63ba643ae74ce](https://github.com/ijacquez/libyaul-build-scripts/tree/10cc8fbe414c54c87f9ad7bc50c63ba643ae74ce).
  Its embedded crosstool-NG directory has separate licenses; those do not
  establish a license for the outer project's README or configuration files.

Availability in a public upstream repository is not a general redistribution
license. See [GitHub's licensing guidance](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository).

## Build selection

The current Makefile defaults to `BUILD_MODE=emulator`. The complete emulator,
`masteronly`, `emulatorprobe`, and `mvp` modes select SameBoy Core sources.
`mvpfast*` modes select PicoGB sources; the PPU-only slave probe selects PicoGB's
`ppu.c`, and the audio modes also select `apu.c`. The diagnostic/startup probes
do not select an emulation core. The main link rules use libYaul and libgcc.

Peanut-GB, gnuboy, SCSP_poneSound, and the SAROO reference tree are not selected
by the main emulator Makefile. crosstool-NG and RGBDS are build-time tools.
Using a compiler does not by itself impose that compiler's license on the
application. libgcc has the GCC Runtime Library Exception; retain the applicable
runtime notices and meet the exception's conditions for any binary release.
Bundling a complete compiler/toolchain has its own distribution obligations.

The export retains libYaul's tracked component build templates, two `.cxx`
sources, and `libyaul/common/wrap-error`. These source/build inputs do not
establish redistribution permission for the withheld Sega IP blobs or external
build-script configuration, and their restoration does not verify a fresh SDK
rebuild. See [Building](BUILDING.md).

## File-specific exceptions

### SameBoy

`Core` and `BootROMs` are covered by the Expat/MIT grant in `LICENSE`, subject
to more specific per-file notices. The local
`BootROMs/hardware.inc` has its own **CC0-1.0** waiver and upstream attribution,
which take precedence for that definitions file. It contains an unused 48-byte
cartridge-header logo macro, not a cartridge or BIOS dump; the retained boot
assembly sources do not invoke that macro. Preserve the file's notice and
[CC0 reference](https://creativecommons.org/publicdomain/zero/1.0/).
The local
Game Boy boot binaries are documented as assemblies of those open sources in
`src/saturn/bootroms/README.md`; their sources and rebuild script are retained.
They are not identified as Nintendo boot-ROM dumps.
Rebuilding these open boot sources also needs their licensed source asset
`BootROMs/SameBoyLogo.png`, the upstream source/build inputs, and RGBDS. An
image-extension filter should not mistake that source asset for a game
screenshot or a cartridge ROM.

The upstream `iOS` directory has an additional written-permission condition for
distribution through a digital marketplace. `HexFiend/License.txt` contains a
BSD 2-clause notice, copyright 2005-2009 Peter Ammon. Both directories are
withheld from the proposed public export and neither is linked into knovGBC.
The libretro API headers contain their own MIT notices for the RetroArch team.

### libYaul

The root MIT license is not the only license in the tree. Preserve at least
the following embedded notices whenever their covered files are included:

| Files or family | Embedded terms / attribution |
| --- | --- |
| `libyaul/kernel/mm/memb.c`, `memb.h` | BSD 3-clause; Swedish Institute of Computer Science, 2004, and Israel Jacquez. |
| `libyaul/kernel/mm/tlsf.h` | BSD 3-clause; Matthew Conte, 2006-2016. Its notice expressly states that this implementation has no GPL restriction inherited from the specification. |
| `libyaul/libc/libc/sys/cdefs.h`, `queue.h` | BSD 3-clause; Regents of the University of California. |
| `libyaul/libc/ctype/ctype.c`, `libyaul/libc/stdlib/strtol.c` | Original BSD notice with an advertising clause; Regents of the University of California, 1989 and 1990 respectively. |
| MIT-noticed libc implementations, including `stdio/printf.c` and `string/memcpy.c` | Rich Felker et al., 2005-2014. |
| MIT-noticed libfixmath-derived gamemath files | Authors of libfixmath; preserve each file's attribution. `fix16_sqrt.c` additionally credits Christophe Meessen, 2019. |
| `libyaul/scu/bus/a/cs0/flash/flash.h` and its map header | BSD 2-clause; Anders Montonen, 2012/2015, and original software by ExCyber. |
| `libbcl/{lz,huffman,rle}.c` and counterparts under `tools/bcl` | zlib-style terms; Marcus Geelnard, 2003-2006. Altered source must be plainly marked and the source notice retained. |
| `libbcl/prs.c`, `tools/bcl/prs.c` | Comments report free use with attribution to fuzziqer for compression/decompression, without a bundled formal grant. See the qualification below. |
| Satconv's `satconv.c`, `map.*`, `sprite.*`, `tile.*` | GNU GPL v3 or later; Nathan Misner, 2021; `tools/satconv/COPYING` contains GPL v3. This is a separate host utility, not an emulator link input. |
| Satconv `ezxml.c`, `ezxml.h` | MIT; Aaron Voisine, 2004-2006. |
| Satconv `qdbmp.h` | MIT; Chai Braudo, 2007. |

The two PRS files are an upstream source exception. Their original code and
attribution comments are retained. The comments report fuzziqer's permission;
they do not include the primary permission statement, which has not been
independently verified. This inventory does not assign MIT or zlib-style terms
to those files or certify their redistribution. Source redistribution and any
binary distribution require separate review of the applicable permission and
obligations.

For Berkeley's original BSD notices, the university's 1999 license addendum
removes the advertising condition from its BSD Unix files. Preserve the
original copyright notices and document the addendum; do not infer the same
dispensation for unrelated licensors. The [FreeBSD copyright page](https://www.freebsd.org/copyright/license/)
reproduces the addendum.

### Other source trees

- Peanut-GB's minigb_apu example has its own MIT license, crediting Alex Baines
  (2017) and Mahyar Koshkouei (2019). Its Nuklear example and embedded utility
  components have separate MIT/public-domain notices; retain them rather than
  substituting the Peanut-GB header's copyright. Embedded test ROM data is
  withheld from the proposed export.
- Peanut-GB `test/minctest.h` uses zlib-style terms, copyright 2014-2017 Lewis
  Van Winkle. Its source notice must remain intact and alterations must be
  plainly marked.
- Peanut-GB `examples/mini_fb/MiniFB.h` has no embedded copyright/license
  statement in this checkout. The [MiniFB upstream license](https://github.com/emoon/minifb/blob/master/LICENSE)
  is MIT, copyright 2014 Daniel Collin, but the exact originating revision of
  the copied header has not been verified. Include the applicable upstream
  notice after establishing that provenance; do not automatically assign the
  Peanut-GB author's copyright to the header. The public handoff withholds the
  `mini_fb` example instead of guessing its copied-header provenance.
  Peanut's preserved upstream `test/test.c` also needs the three deliberately
  omitted ROM-data headers; that upstream test is incomplete in the export.
  This does not prevent knovGBC's separate synthetic host-test suite.
- gnuboy `ezmenu.h` and `miniz_tinfl.c` contain MIT notices. `xz/COPYING`
  declares XZ Embedded sources by Lasse Collin and/or Igor Pavlov to be in the
  public domain. These exceptions do not change gnuboy's overall GPL grant.
- The crosstool-NG `LICENSE` describes its default GPL v2 grant, CC BY-SA 2.5
  documentation, upstream-dependent patch licenses, and individual exceptions.
  Any included crosstool-NG source requires its accompanying notices.

## PicoGB and local extensions

PicoGB and modifications to its library remain under GNU LGPL v2.1. The root
MIT file cannot relicense those portions. The change record identifies the
twelve modified upstream files and the two local palette implementations.
The frozen `tools/tests/fixtures/audio056/apu.c` and `apu.h` also retain
PicoGB-derived LGPL 2.1 treatment; they are regression snapshots, not separately
MIT-licensed replacements.

Some implementations under `src/saturn` also adapt PicoGB's internal execution,
lazy-flag, PPU, or device-timing behavior. For public export, use the conservative
LGPL scope listed in [KNOVGBC_CHANGES.md](../third_party/PicoGB/KNOVGBC_CHANGES.md)
instead of attaching a blanket MIT assertion to all local source headers.
Merely including an interface or using a data layout does not establish that
an independently written file is a derivative work.

For a distributed statically linked executable, include the LGPL text, a
prominent PicoGB use notice, the complete corresponding modified library
source, and the application source/object materials and build controls needed
to modify the library and relink the executable. Keep any displayed copyright
notices consistent with the license. See LGPL v2.1 sections 2, 5, and 6 and
the [GNU static-linking FAQ](https://www.gnu.org/licenses/gpl-faq.en.html#LGPLStaticVsDynamic).
A credits file alone is insufficient. Source export and binary-release
compliance are separate checks.

## Materials withheld from the proposed public export

All originals remain in the local working project. Withholding them from an
export is not deletion or removal of a local feature.

- Commercial cartridge ROMs and images containing them. `mvp_rom.sx` embeds
  Tetris and `mvp_rom_sml.sx` embeds Super Mario Land; other modes stage `roms/`
  through `tools/package.py`. Ignoring standalone ROM files does not prevent
  their inclusion in generated binaries or disc images. Users must supply
  their own authorized ROM inputs for local builds.
- All generated executables/disc images and installed/downloaded toolchain
  binaries, including installers and RGBDS executable archives.
- All libYaul/SAROO Saturn IP blobs, including `sys_sec.bin`, `sys_are*.bin`,
  and `sys_init.bin`. libYaul's `ip.sx` includes these bytes in IP.BIN.
  `sys_sec.bin` itself carries Sega's 1994 copyright and all-rights-reserved
  notice. A license on surrounding source does not establish permission to
  redistribute the blobs. Their omission means the public source export does
  not by itself supply a ready-to-distribute Saturn disc image.
- The SAROO reference checkout and the outer libyaul-build-scripts checkout
  whose global redistribution licenses were not identified.
  The local toolchain script refers to its `native-linux.config`; that file is
  not supplied by the export, so obtaining a separately authorized or newly
  authored configuration is also required to reproduce the toolchain path.
- SameBoy iOS/HexFiend, upstream action fixtures containing ROM data,
  Peanut-GB embedded test-ROM headers, and SCSP_poneSound demo media.
- Third-party game screenshots, copyrighted documentation extracts, and other
  media without a separately established publication basis.

The withheld SAROO tree also contains mixed-license fonts: WenQuanYi
`wqy13px.bdf` declares GPL v2 with a font-embedding exception; some Bigelow &
Holmes font files refer to a missing `LEGAL NOTICE`; other fonts carry
Adobe/DEC, Cronyx, or public-domain statements. The local `font_cjk.bin`
generator/input relationship has not been verified. None of these is granted
the knovGBC MIT license or cleared by this inventory.

The local libYaul default font has its own Israel Jacquez/MIT header; the small
HUD glyphs are represented directly in `src/saturn/platform_video.c`. This
inventory records the stated notices and does not independently establish the
authorship history of every bitmap glyph.

Before any binary release, review the final artifact contents, satisfy the
applicable software-license obligations, and resolve the rights to every
included ROM, boot/security blob, font, and media asset. This source inventory
does not certify Sega security-blob redistribution or any complete toolchain
package.
