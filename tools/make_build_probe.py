#!/usr/bin/env python3
"""Validate a publication ROM or create knovGBC's original build fixture."""
from __future__ import annotations

import argparse
from pathlib import Path


def validate_rom(data: bytes, rom_class: str) -> None:
    expected_size = 0x10000 if rom_class == "dmg-mbc1" else 0x8000
    expected_type = 1 if rom_class == "dmg-mbc1" else 0
    expected_rom_size = 1 if rom_class == "dmg-mbc1" else 0
    if len(data) != expected_size:
        raise ValueError(f"expected exactly {expected_size} bytes, got {len(data)}")
    if data[0x143] & 0x80:
        raise ValueError("this profile requires a DMG cartridge (CGB flag clear)")
    if data[0x147] != expected_type:
        raise ValueError(f"expected cartridge type {expected_type}, got {data[0x147]}")
    if data[0x148] != expected_rom_size or data[0x149] != 0:
        raise ValueError("unsupported ROM/RAM size header for this profile")


def make_probe() -> bytes:
    """An original 32 KiB no-MBC fixture with zero-filled logo bytes.

    The post-boot entry jumps to an idle loop. The fixture intentionally has no
    Nintendo logo, copied boot code, commercial assets, or playability promise.
    """
    data = bytearray(0x8000)
    data[0x100:0x104] = bytes((0x00, 0xC3, 0x50, 0x01))
    data[0x134:0x141] = b"KNOVGBC PROBE"
    data[0x14A] = 1
    data[0x150:0x153] = bytes((0xF3, 0x18, 0xFE))
    data[0x14D] = (-sum(data[0x134:0x14D]) - 25) & 0xFF
    checksum = sum(data) & 0xFFFF
    data[0x14E:0x150] = checksum.to_bytes(2, "big")
    return bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--rom", type=Path)
    source.add_argument("--probe", action="store_true")
    parser.add_argument("--class", dest="rom_class", choices=("dmg-rom-only", "dmg-mbc1"), default="dmg-rom-only")
    parser.add_argument("--check", action="store_true", help="validate only; never write files")
    parser.add_argument("--output-rom", type=Path)
    parser.add_argument("--output-assembly", type=Path)
    args = parser.parse_args()
    if args.probe and args.rom_class != "dmg-rom-only":
        parser.error("the 32 KiB fixture cannot use the MBC1 profile")
    if args.rom is None and not args.probe:
        if args.check:
            print("No ROM selected; a build requires --rom FILE or --probe.")
            return 0
        parser.error("select --rom FILE or --probe")
    data = make_probe() if args.probe else args.rom.read_bytes()
    validate_rom(data, args.rom_class)
    if args.check:
        label = "original build fixture" if args.probe else str(args.rom)
        print(f"Input valid: {label} ({len(data)} bytes, {args.rom_class}).")
        return 0
    if args.output_rom is None or args.output_assembly is None:
        parser.error("both --output-rom and --output-assembly are required")
    output_rom = args.output_rom.resolve()
    output_assembly = args.output_assembly.resolve()
    if args.rom is not None and args.rom.resolve() in (output_rom, output_assembly):
        parser.error("output must not overwrite the source ROM")
    if output_rom == output_assembly:
        parser.error("ROM and assembly output paths must differ")
    incbin = output_rom.as_posix().replace("\\", "\\\\").replace('"', '\\"')
    assembly = (
        "/* Generated publication input; the emulator sources are unchanged. */\n"
        '.section .data.knovgbc_mvp_rom, "aw"\n'
        ".align 4\n"
        ".global _knovgbc_mvp_rom\n"
        ".global _knovgbc_mvp_rom_end\n"
        "_knovgbc_mvp_rom:\n"
        f'        .incbin "{incbin}"\n'
        "_knovgbc_mvp_rom_end:\n"
    )
    output_rom.parent.mkdir(parents=True, exist_ok=True)
    output_assembly.parent.mkdir(parents=True, exist_ok=True)
    output_rom.write_bytes(data)
    with output_assembly.open("w", encoding="utf-8", newline="\n") as output:
        output.write(assembly)
    print(f"Prepared isolated ROM input and assembly: {output_rom.parent}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        raise SystemExit(f"error: {error}")
