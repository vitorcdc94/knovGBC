# Known issues and continuation ideas

## Development limitations

- Incomplete; original development ended, with no future support promised.
- Recent path is DMG-only, one cartridge per build, with strict header checks.
  GBC, broad mapper compatibility, persistent saving and accessories have not
  been validated on Saturn. Upstream capability is not port validation.
- No-audio Tetris reached near-real-time in reports, but 056 audio reduced
  gameplay to about 24.5 FPS. 057's equivalent mixer did not visibly improve it.
- Smaller code did not guarantee speed: 057 trades branches/accesses for
  three extra variable multiplies. Audio equivalence is not a speedup.
- SCSP plays in real time while emulation may fall behind. Starvation inserts
  silence, not time stretching; long stalls trigger recovery.
- Sound primes about 125 ms initially. Its hardware pitch differs slightly
  from the producer rate; long sessions can drift.
- APU analog DAC discharge and model-specific envelope glitches are not fully
  modeled. Timing follows instruction boundaries, not every bus transaction.
- Retained assembly/Slave/VDP1 trials did not establish a faster recent baseline.
- Existing compiler warnings and linker RWX warning remain unresolved.
- PAL, alternative loaders and extensive game compatibility are not covered.

## Reproduction/publication limitations

- Installed compilers/SDK are not redistributed. A clean SDK bootstrap has
  not been freshly verified.
- The export restores eleven libYaul component build templates, two `.cxx`
  sources, and `libyaul/common/wrap-error`. These source inputs do not resolve
  the withheld Sega security blobs or unlicensed external configuration, and
  their restoration is not a verified fresh SDK rebuild.
- libYaul bootstrap/security blobs carry Sega copyright text; redistribution
  permission was not established. Source/ELF and disc packaging are separate
  verification steps. Those blobs are withheld from the public source copy.
- SAROO references and libyaul-build-scripts configs lack clear inspected
  root licenses; they are withheld, not relicensed MIT.
- libYaul's `libbcl/prs.c` and `tools/bcl/prs.c` retain comments reporting free
  use with credit to fuzziqer, without a bundled formal grant. The primary
  permission has not been independently verified; these are separate source
  exceptions, not covered by the archive's MIT or zlib-style descriptions.
- Integration tests generally need private Tetris/SML ROMs and sometimes
  enforce their titles. Public synthetic tests cover a different scope.
- Legacy helpers assume the old WSL environment. New portable helpers are
  provided and the exported legacy launcher is parameterized for privacy.
- No root Git history existed. Local vendor histories remain intact;
  public vendored source is accompanied by upstream revision records.

## Possible future work (not implemented)

Investigate audio generation, scheduling and copying separately before a
larger rewrite. Candidates include batching APU dispatch while preserving
sound I/O timing, cheaper oscillator advancement and asynchronous sound-RAM
transfer. Another processor is a larger architectural change, not a proven fix.

Keep correctness comparisons/no-audio baselines. Broaden compatibility with
clearly licensed test ROMs, generalize the cartridge frontend and resolve
bootstrap provenance before binary releases. None of these are measured gains
or promises that the original developer will resume work.
