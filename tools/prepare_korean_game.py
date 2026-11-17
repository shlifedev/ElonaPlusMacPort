#!/usr/bin/env python3
"""Generate an opt-in display-hook source from the verified private 2.335 source.

The original CP932 bytes are never decoded/re-encoded. Output remains private
under work/, and is not selected by either player launcher. This requires the
native type-48 display command; the official Windows runtime cannot run it.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE_SHA256 = "c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55"
COMMAND_TYPE = 48
COMMAND_ID = 0
DECLARATIONS = b"#regcmd 48\r\n#cmd native_ko_gmes $00\r\n"
ANCHOR = (b"#deffunc gmes str gmes_prm0\r\n"
          b'\tm@m111 = ""\r\n'
          b'\tmsg@m111 = "" + gmes_prm0 + "$end"\r\n')
ENTRY = b"#deffunc gmes str gmes_prm0\r\n"
HOOK = (b"\tnative_ko_gmes gmes_prm0, gmesx, gmesy, gmesw, gmestype, gmescol, gmescol(1), gmescol(2)\r\n"
        b"\tif ( stat >= 0 ) {\r\n"
        b"\t\tgmesy += stat\r\n"
        b"\t\treturn 0\r\n"
        b"\t}\r\n")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def transform(source: bytes) -> tuple[bytes, list[dict]]:
    """Hash-pinned, insertion-only transform; unknown/changed inputs fail closed."""
    if digest(source) != SOURCE_SHA256:
        raise ValueError("source SHA-256 differs from the verified 2.335 reference")
    if source.count(ANCHOR) != 1 or source.count(ENTRY) != 1:
        raise ValueError("gmes entry/context anchor must occur exactly once")
    if b"native_ko_gmes" in source or b"#regcmd" in source:
        raise ValueError("input already declares native commands; review type ownership")
    offset = source.index(ENTRY) + len(ENTRY)
    output = DECLARATIONS + source[:offset] + HOOK + source[offset:]
    # Independently remove the two exact inserted spans and assert all original
    # bytes, including mixed newlines and ambiguous CP932 pairs, are untouched.
    restored = output[len(DECLARATIONS):]
    restored = restored[:offset] + restored[offset + len(HOOK):]
    if restored != source:
        raise AssertionError("insertion changed original bytes")
    changes = [
        {"kind": "declare_native_command", "input_byte_offset": 0,
         "output_byte_offset": 0, "inserted_bytes": len(DECLARATIONS),
         "inserted_lines": DECLARATIONS.count(b"\n"), "sha256": digest(DECLARATIONS)},
        {"kind": "gmes_display_boundary", "input_byte_offset": offset,
         "input_next_line": source[:offset].count(b"\n") + 1,
         "output_byte_offset": len(DECLARATIONS) + offset,
         "inserted_bytes": len(HOOK), "inserted_lines": HOOK.count(b"\n"),
         "sha256": digest(HOOK), "context_sha256": digest(ANCHOR)},
    ]
    return output, changes


def command_counts(ax: bytes) -> dict:
    """Inspect instruction tokens only, without interpreting external tables."""
    if len(ax) < 96 or ax[:4] != b"HSP3":
        raise ValueError("not a complete HSP3 object")
    version, _, total, start, size = struct.unpack_from("<5I", ax, 4)
    if version not in (0x301, 0x360) or start not in (96, 112) or total != len(ax):
        raise ValueError("unsupported AX header/version/size")
    if start + size > len(ax) or size % 2:
        raise ValueError("invalid AX code section")
    counts = Counter()
    position = start
    end = start + size
    total_tokens = 0
    while position < end:
        if position + 4 > end:
            raise ValueError("truncated AX token")
        kind, flags = ax[position:position + 2]
        if flags & 0x0F:
            raise ValueError("unsupported token flags")
        length = 6 if flags & 0x80 else 4
        if kind == 11 and flags & 0x20:
            length += 2
        if position + length > end:
            raise ValueError("truncated AX token value/branch")
        value = struct.unpack_from("<i" if flags & 0x80 else "<H", ax, position + 2)[0]
        if kind == COMMAND_TYPE:
            counts[value] += 1
        total_tokens += 1
        position += length
    return {"object_version": f"0x{version:03x}", "header_bytes": start,
            "total_tokens": total_tokens,
            "type48_commands": {str(k): v for k, v in sorted(counts.items())}}


def check_output_path(path: Path) -> Path:
    # Reject symlink components before resolve() erases evidence of them.
    absolute = path.absolute()
    if any(p.is_symlink() for p in (absolute, *absolute.parents)):
        raise ValueError("output path must not contain symlinks")
    resolved = absolute.resolve()
    work = (ROOT / "work").resolve()
    if not resolved.is_relative_to(work) or resolved == work:
        raise ValueError("output must be a new directory strictly below work/")
    if resolved.exists():
        raise ValueError("output already exists; choose a new directory")
    return resolved


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", type=Path, default=ROOT / "work/decompiled/2.335-reference/start.hsp")
    ap.add_argument("--output", type=Path, required=True, help="new private directory below work/")
    ap.add_argument("--compile", action="store_true", help="compile and require exactly one type48/$00 token")
    ap.add_argument("--compiler", type=Path, default=ROOT / "build/native-macos-arm64-legacy-cp932-release/elona_hspcmp")
    ap.add_argument("--common", type=Path, default=ROOT / ".cache/upstream/OpenHSP/common")
    args = ap.parse_args()
    try:
        destination = check_output_path(args.output)
        original = args.source.read_bytes()
        output, changes = transform(original)
        compiler = args.compiler.resolve()
        common = args.common.resolve()
        if args.compile and (not compiler.is_file() or not common.is_dir()):
            raise ValueError("compiler/common directory missing")
        destination.mkdir(parents=True, exist_ok=False)
        (destination / "start.hsp").write_bytes(output)
        report = {"schema_version": 1, "game_version": "2.335",
                  "source": str(args.source.resolve()), "source_sha256": digest(original),
                  "output_source_sha256": digest(output), "source_encoding": "CP932 bytes preserved",
                  "original_bytes_unchanged": True, "changes": changes,
                  "abi": {"type": COMMAND_TYPE, "command": COMMAND_ID, "arguments": 8,
                          "result": "stat < 0: original fallback; stat >= 0: rendered height; gmes returns stat 0"},
                  "opt_in_only": True, "launcher_modified": False, "compiled": False,
                  "executed": False}
        success = True
        if args.compile:
            command = [str(compiler), "--compath=" + str(common) + "/",
                       "-o" + str(destination / "start.ax"), str(destination / "start.hsp")]
            with (destination / "compile.log").open("wb") as log:
                process = subprocess.run(command, cwd=destination, stdout=log, stderr=subprocess.STDOUT)
            report.update(compile_command=command, compiler_sha256=digest(compiler.read_bytes()),
                          compile_returncode=process.returncode)
            success = process.returncode == 0 and (destination / "start.ax").is_file()
            if success:
                blob = (destination / "start.ax").read_bytes()
                report.update(compiled=True, ax_sha256=digest(blob), ax_bytes=len(blob))
                try:
                    report["token_check"] = command_counts(blob)
                    success = report["token_check"]["type48_commands"] == {"0": 1}
                    if not success:
                        report["error"] = "compiled AX must contain exactly one type48/$00 call"
                except ValueError as exc:
                    report["error"] = str(exc)
                    success = False
            else:
                report["error"] = "compile failed; see compile.log"
        report["preparation_passed"] = success
        (destination / "manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"output": str(destination), "prepared": success,
                          "compiled": report["compiled"], "executed": False}, indent=2))
        return 0 if success else 1
    except (OSError, ValueError) as exc:
        print(f"Korean display preparation failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
