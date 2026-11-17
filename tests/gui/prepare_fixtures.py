#!/usr/bin/env python3
"""Encode authored GUI fixtures as CP932. Does not compile or execute them."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).resolve().parent
NAMES = ("pixel_roundtrip", "bitmap_export", "input_control", "color_key")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "work/gui-fixtures")
    args = parser.parse_args()
    output = args.output.resolve()
    original = (ROOT / "elonaplus2.335").resolve()
    if output == original or original in output.parents:
        raise SystemExit("Output must be separate from the original game directory")
    if args.output.is_symlink() or not any(parent.resolve() in output.parents for parent in (ROOT / "work", ROOT / "build")):
        raise SystemExit("Output must be a non-symlink subdirectory under project work/ or build/")
    payloads = {}
    rows = []
    for name in NAMES:
        source = (SOURCE / (name + ".hsp")).read_bytes()
        encoded = source.decode("utf-8").encode("cp932")
        payloads[name + ".hsp"] = encoded
        rows.append({"name": name, "utf8_source_sha256": hashlib.sha256(source).hexdigest(),
                     "cp932_source_sha256": hashlib.sha256(encoded).hexdigest(), "executed": False})
    payloads["manifest.json"] = (json.dumps({"encoding": "CP932", "fixtures": rows,
        "scope": "Authored synthetic GUI fixtures; generation only, no compilation or execution",
        "warning": "bitmap_export writes fixture-buffer.bmp when executed; use a dedicated scratch directory"},
        indent=2) + "\n").encode("utf-8")
    # Verify every existing source before writing anything; never replace edits.
    for name, contents in payloads.items():
        destination = output / name
        if destination.is_symlink() or (destination.exists() and destination.read_bytes() != contents):
            raise SystemExit(f"Existing output differs or is a symlink: {destination}")
    output.mkdir(parents=True, exist_ok=True)
    for name, contents in payloads.items():
        destination = output / name
        if not destination.exists():
            with destination.open("xb") as stream:
                stream.write(contents)
    print(f"Prepared {len(rows)} CP932 GUI fixtures in {output}; none executed")


if __name__ == "__main__":
    main()
