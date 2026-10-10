#!/usr/bin/env python3
"""Write a single-track CUE descriptor for a Saturn ISO/BIN payload."""
from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", type=Path, required=True, dest="bin_path")
    parser.add_argument("--cue", type=Path, required=True, dest="cue_path")
    args = parser.parse_args()
    if not args.bin_path.is_file():
        parser.error(f"payload not found: {args.bin_path}")
    args.cue_path.parent.mkdir(parents=True, exist_ok=True)
    args.cue_path.write_text(
        f'FILE "{args.bin_path.name}" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n',
        encoding="ascii",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

