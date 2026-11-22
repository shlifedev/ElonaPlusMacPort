#!/usr/bin/env python3
"""Restore two verified 2.335 control-flow details without changing the raw decode.

The pinned borscht merges a second `else` into the first else body. HSP 3.4
accepts this legacy syntax and its bytecode skips that second body differently.
An explicit goto after `stop` also duplicates the compiler-generated goto.
See docs/WINDOWS_REFERENCE.md for bytecode offsets and the small reproducer.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RAW_SHA = "dce3e305f91e377fd669a045d9dce7cc197509d094852e56b67be312aa233725"


def publish(directory, files):
    if directory.is_symlink():
        raise ValueError("Output directory must not be a symlink")
    if directory.exists():
        if not directory.is_dir() or {p.name for p in directory.iterdir()} != set(files):
            raise ValueError("Existing output directory has unexpected contents")
        for name, contents in files.items():
            path = directory / name
            if path.is_symlink() or not path.is_file() or path.read_bytes() != contents:
                raise ValueError("Existing output differs or is a symlink: " + name)
        return
    directory.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".control-flow-", dir=directory.parent))
    try:
        for name, contents in files.items():
            with (stage / name).open("xb") as stream:
                stream.write(contents)
        if directory.exists() or directory.is_symlink():
            raise ValueError("Output appeared during publication")
        os.rename(stage, directory)
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def transform(raw):
    if hashlib.sha256(raw).hexdigest() != RAW_SHA:
        raise ValueError("Only the exact pinned 2.335 raw decode is supported")
    lines = raw.splitlines(keepends=True)
    # Original line numbers are stable only under the exact input hash above.
    # Apply in descending order so source coordinates never drift.
    if lines[232678].strip() != b"goto *label_4600":
        raise ValueError("Stop ghost-goto context mismatch")
    del lines[232678]
    newline = b"\r\n" if lines[213229].endswith(b"\r\n") else b"\n"
    lines[213229:213229] = [b"\t" * 6 + b"}" + newline, b"\t" * 6 + b"else {" + newline]
    return b"".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=ROOT / "work/decompiled/2.335/start.hsp")
    parser.add_argument("--output", type=Path, default=ROOT / "work/decompiled/2.335-hsp34/start.hsp")
    args = parser.parse_args()
    if args.output.is_symlink() or args.output.parent.is_symlink():
        parser.error("Output and its directory must not be symlinks")
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "work/decompiled"):
        parser.error("Output must be inside work/decompiled")
    if output.suffix != ".hsp":
        parser.error("Output filename must end in .hsp")
    raw = args.input.read_bytes()
    fixed = transform(raw)
    report = {"rawSha256": RAW_SHA, "outputSha256": hashlib.sha256(fixed).hexdigest(),
              "outputBytes": len(fixed), "encodingChanged": False,
              "changes": [
                  {"originalSourceLine": 213230, "originalCsByteOffset": 3507426,
                   "action": "Restore the dropped second else opcode using HSP 3.4 legacy syntax"},
                  {"originalSourceLine": 232679, "originalCsByteOffset": 4000184,
                   "action": "Remove explicit goto already emitted automatically by stop"}],
              "gameBehaviorVerified": False}
    files = {output.name: fixed,
             output.with_suffix(".manifest.json").name: (json.dumps(report, indent=2) + "\n").encode("utf-8")}
    # Publish both files together; an existing directory is accepted only if its
    # exact file set and every byte match, with symlinks explicitly rejected.
    publish(output.parent, files)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
