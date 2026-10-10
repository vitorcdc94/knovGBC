# knovGBC — Game Boy/Game Boy Color on Sega Saturn

`knovGBC` ports the [SameBoy](https://sameboy.github.io/) emulation core to
the Sega Saturn using Yaul. The project does not include ROMs or BIOS files:
use only game dumps that you have the right to use.

## Current status

The local environment and the first Saturn image have already been compiled
and structurally validated. The image contains the Saturn executable, CD loader,
controller input and VDP2 video output.

This is still an early port, not the final emulator. Hardware/Saroo testing is
required before it can be considered compatible. Current limitations are:

- startup loads only the first prepared ROM (`R0000`), rather than
  displaying a menu;
- each ROM is limited to 1 MiB because of the Saturn's current RAM budget;
- SCSP audio, persistent saves and a selection interface have not yet
  been integrated;
- the generated image was validated on a PC but has not yet been tested on
  a real Saturn or Saroo.

## Preparing a ROM

1. Copy a `.gb` or `.gbc` ROM of up to 1 MiB into `roms/`.
2. For the first test, leave only one ROM in that folder.
3. Run the following in PowerShell from the project root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build_project.ps1 -Target package
```

The bypass above applies only to that execution; it does not change the
Windows script execution policy.

The command generates:

- `dist/knovgbc.bin` — Saturn data image;
- `dist/knovgbc.cue` — image track descriptor.

The CUE references the BIN by name, so both files must remain together,
ideally in a separate subfolder for each game. For Saroo, copy that subfolder
to `SAROO/ISO/` (or the image folder configured in your cartridge's firmware).
The ROM is contained in the image; it is not individually converted
to BIN/CUE.

## Current controls

| Game Boy | Saturn controller |
| --- | --- |
| D-pad | D-pad |
| A | A |
| B | B |
| Start | Start |
| Select | L |
| Fast-forward | R |

## Structure

- `src/saturn/`: SameBoy integration with Saturn video, CD and controller input.
- `roms/`: place authorized ROMs here for the next package.
- `tools/build_project.ps1`: Windows command that mounts the current folder
  in WSL and invokes the local build without using the external hard drive.
- `dist/`: generated ISO/CUE images.

## Licenses

This project's integration code is MIT. SameBoy retains its Expat license;
see `third_party/SameBoy/LICENSE`.
