# SameBoy boot ROM binaries

These files are assembled from the Expat-licensed sources in
`third_party/SameBoy/BootROMs` with the official RGBDS 1.0.3 Linux release.
They are not Nintendo boot ROM dumps.

- `dmg_boot.bin`: 256 bytes, SHA-256
  `6f64da4cecd7e54e2f928eb3e3ba7810a7a567d0d247cc71737d1771e073a916`
- `cgb_boot_fast.bin`: 2304 bytes, SHA-256
  `734fc76858bc6d78e724df75efe90146c612817f05c5a837409f9ea34c70e26e`

Historical rebuilding used the machine-specific command below. The public
source copy does not include the downloaded RGBDS tools/archive, and the
legacy launcher is not portable. Supply RGBDS 1.0.3 separately, retain the
licensed `SameBoy/BootROMs/SameBoyLogo.png` asset, and adapt
`tools/build_bootroms_wsl.sh` (fixed `/mnt/knovGBC` path) before rebuilding.
The current PicoGB path does not need these boot ROMs. See
`docs/BUILDING.md` for the supported publication build entry point.

Historical command, not verified on a fresh installation:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_project.ps1 -Target bootroms
```
