#!/usr/bin/env python3
"""Stage GB/GBC ROMs into the ISO9660 directory used by Yaul's make-iso."""
from __future__ import annotations

import argparse
import shutil
from pathlib import Path

ROM_EXTENSIONS = {".gb", ".gbc"}
ROM_SIZE_LIMIT = 1024 * 1024


def rom_title(path: Path) -> str:
    data = path.read_bytes()
    if len(data) < 0x150:
        raise ValueError(f"{path}: file is shorter than a valid Game Boy header")
    title = data[0x134:0x144].split(b"\0", 1)[0]
    return title.decode("latin-1", "replace").strip() or path.stem


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument("--roms", type=Path)
    sources.add_argument(
        "--empty",
        action="store_true",
        help="remove previously staged ROMs; used when the ROM is embedded",
    )
    parser.add_argument("--staging", type=Path, required=True)
    args = parser.parse_args()

    if args.roms is not None and not args.roms.is_dir():
        parser.error(f"ROM directory not found: {args.roms}")

    target_roms = args.staging / "ROMS"
    if target_roms.exists():
        shutil.rmtree(target_roms)

    if args.empty:
        print("Cleared staged ROM files; this build embeds its only ROM.")
        return 0

    target_roms.mkdir(parents=True)

    manifest = ["# knovGBC ROM manifest", "# filename\ttitle\ttype"]
    roms = sorted(
        path for path in args.roms.rglob("*")
        if path.is_file() and path.suffix.lower() in ROM_EXTENSIONS
    )
    for index, source in enumerate(roms):
        source_size = source.stat().st_size
        if source_size > ROM_SIZE_LIMIT:
            raise ValueError(
                f"{source}: {source_size} bytes exceeds the current Saturn "
                f"ROM buffer limit of {ROM_SIZE_LIMIT} bytes"
            )
        # ISO9660 level 1 needs short uppercase names. The manifest preserves
        # each original title for the Saturn ROM browser.
        destination_name = f"R{index:04d}{source.suffix.upper()}"
        # The ISO contains only the ROM bytes. Preserving host timestamps and
        # mode bits is unnecessary and can fail on the WSL/NTFS bind mount
        # used by this project after the copy itself has already succeeded.
        shutil.copyfile(source, target_roms / destination_name)
        game_type = "GBC" if source.suffix.lower() == ".gbc" else "GB"
        manifest.append(f"{destination_name}\t{rom_title(source)}\t{game_type}")

    (target_roms / "MANIFEST.TXT").write_text("\n".join(manifest) + "\n", encoding="utf-8")
    print(f"Staged {len(roms)} ROM(s) in {target_roms}.")
    if len(roms) > 1:
        print("Note: the current Saturn runtime starts the first staged ROM (R0000).")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        raise SystemExit(f"error: {error}")
