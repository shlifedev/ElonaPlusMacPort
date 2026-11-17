#!/usr/bin/env python3
"""Extract the pinned original without modifying its installation."""

import argparse
from pathlib import Path
import sys

from elona_extract import ExtractError, MAX_FILE_BYTES, extract, write_outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        executable = args.game_dir / "elonaplus.exe"
        with executable.open("rb") as stream:
            data = stream.read(MAX_FILE_BYTES + 1)
        if len(data) > MAX_FILE_BYTES:
            raise ExtractError("executable exceeds the inspection limit")
        files, manifest = extract(data)
        created = write_outputs(args.output, files, original_dir=args.game_dir)
    except (ExtractError, OSError) as exc:
        print(f"prepare_game: {exc}", file=sys.stderr)
        return 1
    print(f"{'Created' if created else 'Verified identical'}: {args.output}")
    print(f"AX SHA-256: {manifest['summary']['sha256']}")
    print(f"Declarations: {manifest['summary']['library_count']} libraries, "
          f"{manifest['summary']['external_function_count']} external functions")
    print("Static extraction only; game execution and opcode coverage remain unverified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
