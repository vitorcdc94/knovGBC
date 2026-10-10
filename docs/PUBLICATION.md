# Publishing the knovGBC source handoff

This preparation does not publish, initialize Git, commit, or push anything.
The original working project is preserved, including private ROMs, old disc
images, downloaded SDKs and nested upstream Git metadata. **Do not upload that
entire working folder.** Use the separately prepared source copy instead.

## Export contents and exclusions

### Final pre-upload inventory check

The prepared source copy includes a read-only PowerShell checker:

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\verify_publication.ps1
~~~

Run it before building or adding generated files. It checks manifest SHA-256
values and sizes, missing/unlisted files, unexpected executable signatures
(including extensionless files), the two reviewed open boot binaries, and known
private-file/credential markers. Its own root Git metadata is skipped; nested
Git histories are not publication assets. It changes no files and does not
certify copyright ownership, detect every possible secret, compile the emulator,
or validate Saturn behavior. A PASS is a scoped inventory result, not legal
clearance. Update the manifest honestly after any further source-copy edits.

### Windows/Linux line endings

Git can convert text from Windows CRLF to Linux LF on commit or checkout.
The manifest retains its original-source hashes and raw exported hashes/sizes.
Reviewed text entries additionally record `canonical_eol: crlf-to-lf-v1`, a
SHA-256 and a byte count after replacing only CRLF pairs with LF. The checker
accepts either an exact raw match or that explicitly recorded text match.
It does not trim spaces, change encoding, remove a BOM, remove lone CR bytes,
or ignore added/removed lines. Binary assets and NUL-containing files require
exact bytes. An older manifest without these extra fields remains raw-only;
the checker never guesses its missing expected content.

Run the isolated regression tests with Windows PowerShell:

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\tests\publication_line_endings_test.ps1
~~~

This test creates and removes only its own unique temporary fixture directory.
It does not require a commercial ROM or SDK, change source files or contact GitHub.
The corrected exporter writes both raw and eligible-text fingerprints for new
exports. A fingerprint is an inventory comparison, not a digital signature;
the manifest and checker themselves are trusted inputs.

When uploading through the browser, upload the contents of this prepared copy,
not the private working project or the outer staging folder. Browser upload
does not enforce .gitignore. Include the root README, LICENSE, .gitignore,
.gitattributes and .github files/directories, preserving all relative paths.

The final review corrected an earlier extension-only omission: the extensionless
SCSP "PROJ/srcs" executable is withheld. It also restored eleven libYaul source
Makefile templates, two .cxx sources and "libyaul/common/wrap-error". Those are
build inputs, not generated outputs; Sega IP/security blobs remain withheld.
The PRS attribution-permission exception and incomplete historical edit dates
are documented in THIRD_PARTY.md and third_party/PicoGB/KNOVGBC_CHANGES.md. They are not a
blanket MIT grant or a legal-compliance certificate.

`tools/prepare_publication.ps1` makes a fresh copy and optional ZIP under
`publication/`. It refuses existing output names, follows no unreviewed file
links, and uses selected source directories and source/input extensions rather
than assuming `.gitignore` is a security boundary. Its manifest records source
and exported SHA-256 values and all export-only transformations. The ZIP includes
dotfiles. Regeneration example, from this prepared source-copy directory, using
its corrected exporter (the private working project's older exporter was not
modified):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\prepare_publication.ps1 -Name knovGBC-source-new-review
```

The exporter itself uses Windows PowerShell/.NET and needs no WSL, compiler or
ROM. It retains all original knovGBC emulator C/header/assembly files in the
source handoff, all historical build modes, tests, documentation and the
selected licensed source snapshots. It omits generated products, installers,
installed compilers, nested `.git` directories, private logs and cartridge data.
The two specifically hash-checked open SameBoy boot assemblies and their source
provenance are retained; they are not Nintendo boot dumps.

Other withheld material includes Sega IP/security blobs; unlicensed reference
checkouts (SAROO and the outer libyaul-build-scripts); SameBoy iOS/HexFiend and
upstream workflow ROM fixtures; Peanut embedded cartridge headers, screenshots
and the MiniFB example with unresolved copied-header provenance; and most
SCSP_poneSound demo/toolchain/media material. The latter retains its license,
README/documentation and PROJ source/build inputs as a reference, not a runnable
demo package. Other non-source vendor assets are omitted except the reviewed
SameBoy BootROM build logo. See the manifest and [third-party inventory](THIRD_PARTY.md).

This is a source handoff, not a redistributable game or full SDK distribution.
The omissions can prevent historical reference demos or toolchain bootstrap
scripts from running as-is. No local feature or original file was deleted to
create the export. The portable build path accepts an external SDK and a ROM
provided by its user; [Building](BUILDING.md) explains its constraints.

## Precisely what changes in the copy

The original emulator sources are not rewritten. Exported modified PicoGB files,
local library extensions and the frozen 056 APU test snapshot gain an inventory
comment preserving LGPL terms and upstream provenance. The original code body
remains byte-identical after that prefix; the manifest records both hashes.
The inventory date is not an invented historical implementation date.
The exported gnuboy `.gitignore` also gains a clearly marked exception for
`sys/nix/config.h.in`, so Git will include that existing build template rather
than treating it as generated configuration.

The exported historical `tools/build_project.ps1` replaces its author's Windows
folder with `KNOVGBC_LEGACY_SOURCE_PATH` and normalizes that script's line endings.
It still assumes Ubuntu, a root launcher, a Linux user `knov` and a bind mount;
it is retained for history, **not the portable entry point**. Setting that variable
inside its WSL environment and providing those accounts/mounts is an advanced,
unverified legacy operation. Use the new `tools/build.ps1` / `tools/build.sh`.

The sole original Makefile change is an optional `MVP_ROM_SOURCE` build-input
override, with the old default kept. New helpers choose isolated output paths;
they do not change emulation behavior or overwrite old cartridge files.

## License decision

Retaining **MIT for independent original code** is recommended and matches the
existing root LICENSE. It is compatible with retaining separate Expat/MIT
SameBoy and MIT libYaul portions and their exceptions. It is **not** a blanket
MIT grant: PicoGB and derived portions retain LGPL 2.1; gnuboy references retain
GPL terms; embedded notices and other exceptions remain applicable. The export
conservatively groups PicoGB internal implementation extensions with its library.
Review that boundary and the file notices before accepting outside contributions.
Do not replace third-party license files. Source and statically linked binary
distribution have different obligations, described in THIRD_PARTY.md.

## Manual GitHub publication

1. Review the prepared copy, README, license inventory and manifest. Confirm no
   unauthorized assets or sensitive data remain. The marker scan is not a legal
   certification or exhaustive secret scanner. Optionally run a reputable local
   secret scanner before your first commit. Do not add the original `roms`,
   `dist`, `build`, SDK directories or private logs.
2. On GitHub, create an empty repository named **knovGBC** under your own account.
   Choose Public if that is your decision. Do not initialize a second README,
   LICENSE or `.gitignore` there; these are already in the prepared copy.
3. In a terminal **inside the prepared source copy**, run:

   ```bash
   git init -b main
   git add .
   git status --short
   git diff --cached --stat
   git diff --cached --name-only
   ```

   Inspect the staged list before proceeding. There should be no commercial
   `.gb`/`.gbc` files, disc images or Sega `sys_*.bin` files. The only included
   `.bin` files are the two reviewed SameBoy boot-source outputs under
   `src/saturn/bootroms`. Git ignores are not retroactive for tracked files.
4. Only after review, commit and add the URL shown by GitHub:

   ```bash
   git commit -m "Archive knovGBC source, experiments and build documentation"
   git remote add origin https://github.com/YOUR_ACCOUNT/knovGBC.git
   git push -u origin main
   ```

   Replace `YOUR_ACCOUNT` with your account. Authenticate using your own normal
   GitHub flow; never store access tokens in project files or URLs.
5. Confirm the uploaded file list and rendered README. The included host-test
   workflow will run on GitHub only after you choose to push. Its remote execution
   is not claimed as tested beforehand. Consider a repository description such
   as "Incomplete, discontinued Game Boy emulator experiment for Sega Saturn".
   Mark it archived/read-only after upload if you want to prevent new issues/PRs;
   leave it unarchived if you want those channels available. Forking works either way.

No root Git repository/history was found locally, so a first archival commit is
necessary. The export records upstream base commits and retains local modified
sources and historical reports; it does not manufacture a development history.
All existing nested upstream histories remain intact in the private original.

The preceding instructions describe the first export. If the archive is already
published, use the existing repository; do not initialize Git again, recreate
the repository, force-push or rewrite earlier commits.

## Updating the existing repository with GitHub Desktop

For the 2026-10-10 line-ending correction, copy only these seven files from the
prepared source copy into the existing clone, preserving their relative paths:

- `PUBLICATION_MANIFEST.json`
- `docs/PUBLICATION.md`
- `docs/RELEASE_AUDIT.md`
- `tools/prepare_publication.ps1`
- `tools/verify_publication.ps1`
- `tools/publication_integrity.ps1` (new)
- `tools/tests/publication_line_endings_test.ps1` (new)

1. In GitHub Desktop, select the existing **knovGBC** repository and **main**
   branch. Fetch origin; if updates exist, pull them before copying files.
   If unrelated local changes exist, review them first instead of overwriting.
2. Open **Repository > Show in Explorer**. This is the clone that Desktop
   uploads, and may be different from the separately prepared source copy.
3. Copy the seven files above to the matching locations. Do not copy the outer
   `publication` directory, the private project or backup folders. Do not alter
   `.git`. If the prepared copy is already this clone, no copying is needed.
4. Review the **Changes** tab: five modified files and two new files are expected.
   No emulator source, commercial game, disc image or SDK binary belongs in this
   update. Optionally run the checker and regression command from the clone.
5. Enter `Fix publication verification across Windows and Linux line endings`
   in **Summary**, select these seven files, then click **Commit to main** and
   **Push origin**. This creates a normal new commit; earlier commits remain.
6. On GitHub, confirm the changed paths, the new commit and the **Actions** run.
   The existing synthetic host workflow tests the emulator on a host computer;
   it does not run this PowerShell regression script, bootstrap the full Saturn
   SDK, prove licensing clearance or validate real hardware.

Do not create a release containing old Tetris/Super Mario Land BIN/CUE files.
Publishing the source does not authorize publishing the games or the withheld
Saturn security blobs. See [Release audit](RELEASE_AUDIT.md) for verification limits.
