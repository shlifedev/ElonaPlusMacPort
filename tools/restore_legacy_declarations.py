#!/usr/bin/env python3
"""Restore ElonaPlus 2.335 legacy DLL metadata through HSP numeric declarations.

Never changes recovered input source or original game data. Numeric declaration
flags are verified against all four original AX parameter types before writing.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

from audit_compatibility import declarations
from elona_extract import AX_SHA256, inspect_ax, write_outputs


ROOT = Path(__file__).resolve().parents[1]
SOURCE_SHA256 = "a9a6e5cdc6f08a664fb581647f06181667b5943e86de57cd0dfef32567823669"
TYPE_NAMES = {4: "int", -6: "str", -8: "pval", -9: "bmscr", -10: "var",
              -16: "prefstr", -17: "pexinfo", -18: "pdpm", -19: "nullptr"}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def decode_flags(flags: int) -> tuple[list[int], int]:
    """HSP codegen.cpp numeric #func argument encoding (not host ABI)."""
    if flags < 0 or flags & ~0x3B7:
        raise ValueError("unsupported legacy declaration flag bits")
    parameters = [{0: 4, 1: -10, 2: -9, 3: -8}[flags & 3], 4, 4, 4]
    if flags & 3 == 3 and not flags & 0x80:
        raise ValueError("pval flag requires the HSP3 compatibility bit")
    if flags & 4:
        parameters[1] = -6
    if flags & 0x10:
        parameters[3] = -16
    if flags & 0x20:
        parameters[3] = -18
    if flags & 0x200:
        if flags & 3 != 2:
            raise ValueError("exinfo flag requires the bmscr selector")
        parameters[:3] = [-17, -19, -19]
        if not flags & 0x30:
            parameters[3] = -19
    return parameters, 2 | (1 if flags & 0x100 else 0)


def encode_flags(parameters: list[int], target_flags: int) -> int:
    if len(parameters) != 4 or target_flags not in (2, 3):
        raise ValueError("legacy declaration must be a four-parameter statement")
    try:
        flags = {4: 0, -10: 1, -9: 2, -8: 0x83, -17: 0x202}[parameters[0]]
    except KeyError as exc:
        raise ValueError("unsupported legacy first parameter") from exc
    if parameters[1] == -6:
        flags |= 4
    if parameters[3] == -16:
        flags |= 0x10
    if parameters[3] == -18:
        flags |= 0x20
    if target_flags & 1:
        flags |= 0x100
    if decode_flags(flags) != (parameters, target_flags):
        raise ValueError("parameter layout cannot be represented by numeric legacy flags")
    return flags


def restore(source: bytes, inventory: dict, *, expected_count: int | None = None) -> tuple[bytes, list[dict]]:
    """Replace ASCII directive lines only, retaining every other source byte."""
    source_lines = source.splitlines(keepends=True)
    # Decode solely for matching metadata. Do not re-encode the full CP932 file:
    # some CP932 byte pairs map to the same Unicode character.
    source_declarations = declarations(source.decode("cp932").splitlines(), inventory)
    changes = []
    aliases = set()
    for declaration in source_declarations:
        if declaration["alias"].casefold() in aliases:
            raise ValueError("duplicate external alias requires a scoped transformation")
        aliases.add(declaration["alias"].casefold())
        original = inventory["functions"][declaration["ax_function_index"]]
        if original["subid"] != -5:
            continue
        types = [p["type"] for p in original["parameters"]]
        if declaration["declaration_kind"] != "func":
            raise ValueError("legacy declaration is not a #func statement")
        try:
            names = [TYPE_NAMES[t] for t in types]
        except KeyError as exc:
            raise ValueError("unsupported original legacy parameter type") from exc
        if [p.casefold() for p in declaration["source_parameters"]] != names:
            raise ValueError(f"source/AX parameter mismatch at line {declaration['declaration_line']}")
        flags = encode_flags(types, original["target_or_flags"])
        number = declaration["declaration_line"]
        old_line = source_lines[number - 1]
        newline = b"\r\n" if old_line.endswith(b"\r\n") else b"\n" if old_line.endswith(b"\n") else b""
        indentation = old_line[:len(old_line) - len(old_line.lstrip(b" \t"))]
        directive = f'#func {declaration["alias"]} "{declaration["export"]}" ${flags:X}'.encode("ascii")
        new_line = indentation + directive + newline
        source_lines[number - 1] = new_line
        changes.append({"ax_function_index": original["index"], "source_line": number,
                        "library": declaration["library"], "alias": declaration["alias"],
                        "export": declaration["export"], "original_subid": -5,
                        "parameter_types": types, "target_flags": original["target_or_flags"],
                        "legacy_numeric_flags": flags, "replacement_declaration": directive.decode("ascii"),
                        "input_line_sha256": digest(old_line), "output_line_sha256": digest(new_line)})
    if expected_count is not None and len(changes) != expected_count:
        raise ValueError(f"expected {expected_count} legacy declarations, found {len(changes)}")
    return b"".join(source_lines), changes


def check_output_path(output: Path, input_dirs: list[Path]) -> None:
    resolved = output.resolve()
    if ROOT / "work" not in resolved.parents:
        raise ValueError("output must be beneath the repository work directory")
    for directory in input_dirs:
        original = directory.resolve()
        if resolved == original or original in resolved.parents or resolved in original.parents:
            raise ValueError("output must be separate from every input directory")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "work/decompiled/2.335-hsp34/start.hsp")
    parser.add_argument("--original-ax", type=Path, default=ROOT / "work/extracted/2.335/start.ax")
    parser.add_argument("--output", type=Path, default=ROOT / "work/decompiled/2.335-reference")
    args = parser.parse_args()
    try:
        source, ax = args.source.read_bytes(), args.original_ax.read_bytes()
        if digest(source) != SOURCE_SHA256 or digest(ax) != AX_SHA256:
            raise ValueError("input source or original AX does not match the pinned 2.335 baseline")
        check_output_path(args.output, [args.source.parent, args.original_ax.parent, ROOT / "elonaplus2.335"])
        result, changes = restore(source, inspect_ax(ax), expected_count=54)
        manifest = {"schema_version": 1, "source_sha256": digest(source), "original_ax_sha256": digest(ax),
                    "output_sha256": digest(result), "encoding": "cp932", "changed_declarations": len(changes),
                    "transformation": "ASCII #func type lists to verified HSP numeric legacy flags; all other bytes retained",
                    "changes": changes,
                    "scope": "Restores serialized DLL declaration intent. Runtime and complete source equivalence remain unverified."}
        files = {"start.hsp": result,
                 "manifest.json": (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")}
        created = write_outputs(args.output, files, original_dir=ROOT / "elonaplus2.335")
    except (OSError, ValueError, KeyError) as exc:
        print(f"restore_legacy_declarations: {exc}", file=sys.stderr)
        return 1
    print(f"{'Created' if created else 'Verified identical'}: {args.output}")
    print(f"Restored {len(changes)} legacy declarations; source SHA-256: {digest(result)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
