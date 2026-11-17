#!/usr/bin/env python3
"""Decode the pinned ElonaPlus 2.335 AX; never apply older semantic databases."""

from __future__ import annotations

import argparse
from collections import Counter
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

from bootstrap_borscht import MANIFEST as BUILD_MANIFEST, PATCH, ROOT, SOURCE, UPSTREAM_COMMIT, sha256

EXPECTED_AX_SHA256 = "f1b1404060faa01dc1d3e46b9a58ba0c39cfd185afd33bdafc2287ee3e921d25"
OUTPUT_ROOT = ROOT / "work" / "decompiled"


def token_inventory(data: bytes, dictionary: bytes) -> dict:
    """Account for code-segment bytes using the upstream lexical dictionary.

    This is structural coverage, not proof of correct reconstructed semantics.
    """
    if len(data) < 96 or data[:4] != b"HSP3":
        raise ValueError("Expected a complete legacy HSP3 header")
    declared, code_offset, code_size = struct.unpack_from("<III", data, 12)
    if declared != len(data) or code_offset < 96 or code_size % 2 or code_offset + code_size > len(data):
        raise ValueError("Invalid HSP3 code bounds")
    text = dictionary.decode("cp932")
    code_section = text.split("$Code", 1)[1].split("$End", 1)[0]
    entries = {}
    for row in csv.reader(io.StringIO(code_section)):
        if not row or not row[0].startswith("0x"):
            continue
        key = (int(row[0], 16), None if row[1] == "-1" else int(row[1], 16))
        entries[key] = row
    pos = code_offset
    end = pos + code_size
    counts: Counter = Counter()
    kinds: Counter = Counter()
    dll_calls: Counter = Counter()
    unknowns = []
    token_count = 0
    while pos < end:
        start = pos
        if pos + 4 > end:
            raise ValueError(f"Truncated token at CS byte {pos - code_offset}")
        kind, flags = data[pos:pos + 2]
        if flags & 0x0F:
            raise ValueError(f"Unsupported token type/flags at CS byte {pos - code_offset}")
        size = 6 if flags & 0x80 else 4
        if pos + size > end:
            raise ValueError(f"Truncated token value at CS byte {pos - code_offset}")
        value = struct.unpack_from("<i" if size == 6 else "<H", data, pos + 2)[0]
        pos += size
        row = entries.get((kind, None if value == -1 else value), entries.get((kind, None)))
        if row:
            if "HasExtraInt16" in row and flags & 0x20:
                pos += 2
                if pos > end:
                    raise ValueError(f"Truncated extra word at CS byte {start - code_offset}")
            if row[3] == "None":
                unknowns.append({"cs_byte_offset": start - code_offset, "type": kind, "value": value})
            # Value-specific command counts, without storing string/variable contents.
            if kind in (8, 9, 10, 11, 13, 14, 15, 17):
                counts[f"{kind:02x}:{value & 0xffffffff:08x}:{row[2]}"] += 1
        elif kind >= 0x12:
            counts[f"plugin:{kind - 0x12}:{value}"] += 1
        else:
            unknowns.append({"cs_byte_offset": start - code_offset, "type": kind, "value": value})
        if kind == 0x10:
            dll_calls[str(value)] += 1
        kinds[f"0x{kind:02x}"] += 1
        token_count += 1
    return {
        "code_offset": code_offset,
        "code_bytes": code_size,
        "bytes_accounted_for": pos - code_offset,
        "tokens": token_count,
        "unknown_count": len(unknowns),
        "unknowns_first_100": unknowns[:100],
        "token_types": dict(sorted(kinds.items())),
        "commands": dict(sorted(counts.items())),
        "dll_function_index_references": dict(sorted(dll_calls.items(), key=lambda pair: int(pair[0]))),
        "limitation": "Static lexical accounting only; counts are code references, not runtime execution counts.",
    }


def load_tool() -> tuple[Path, dict]:
    manifest = json.loads(BUILD_MANIFEST.read_text(encoding="utf-8"))
    binary = Path(manifest["binary"])
    actual_commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=SOURCE, text=True).strip()
    if manifest["upstream_commit"] != UPSTREAM_COMMIT or actual_commit != UPSTREAM_COMMIT:
        raise ValueError("The cached tool is not at the pinned commit; rerun bootstrap_borscht.py")
    for key, path in (("binary_sha256", binary), ("patch_sha256", PATCH),
                      ("dictionary_sha256", SOURCE / "Dictionary.csv"),
                      ("cargo_lock_sha256", SOURCE / "Cargo.lock")):
        if sha256(path) != manifest[key]:
            raise ValueError(f"Tool provenance changed: {key}; rerun bootstrap_borscht.py")
    return binary, manifest


def decompile(input_path: Path, output: Path, verify_repeat: bool) -> dict:
    input_path = input_path.resolve(strict=True)
    output = output.resolve()
    output_root = OUTPUT_ROOT.resolve()
    if not output.is_relative_to(output_root) or output == output_root:
        raise ValueError(f"Output must be a child of {output_root}")
    if input_path.is_relative_to(output):
        raise ValueError("Output may not contain the input")
    data = input_path.read_bytes()
    input_sha = hashlib.sha256(data).hexdigest()
    if input_sha != EXPECTED_AX_SHA256:
        raise ValueError(f"Unsupported AX identity: {input_sha}; this workflow is pinned to ElonaPlus 2.335")
    binary, build = load_tool()
    inventory = token_inventory(data, (SOURCE / "Dictionary.csv").read_bytes())
    if inventory["unknown_count"]:
        raise ValueError(f"Found {inventory['unknown_count']} unknown lexical tokens; refusing to claim full decode coverage")
    if output.exists() and not verify_repeat:
        raise ValueError(f"Output already exists: {output}. Use --verify-repeat to compare without overwriting it")
    if verify_repeat and not output.is_dir():
        raise ValueError("--verify-repeat requires an existing output directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=output.name + ".run-", dir=output.parent))
    command = [str(binary), "decode", str(input_path), "--output-dir", str(stage)]
    env = os.environ.copy()
    env["RUST_BACKTRACE"] = "1"
    with (stage / "decode.log").open("wb") as log:
        result = subprocess.run(command, cwd=SOURCE, env=env, stdout=log, stderr=subprocess.STDOUT)
    generated = stage / (input_path.stem + ".hsp")
    report = {
        "schema_version": 1,
        "game_version": "2.335",
        "input": str(input_path),
        "input_sha256": input_sha,
        "tool": build,
        "command": command,
        "cwd": str(SOURCE),
        "exit_code": result.returncode,
        "token_inventory": inventory,
        "semantic_database": None,
        "status": "decoder_failed",
        "compile_verified": False,
        "behavior_verified": False,
        "limitations": [
            "No plus2.15R or other stale semantic database was applied.",
            "Lexical coverage and successful source emission do not prove recompilation or behavior equivalence.",
            "Only the exact pinned AX is accepted; the upstream decoder contains unchecked binary reads.",
        ],
    }
    if generated.exists():
        source = generated.read_bytes()
        try:
            source.decode("cp932", errors="strict")
            enc_status = "valid"
        except UnicodeDecodeError as exc:
            enc_status = f"invalid at byte {exc.start}"
        report["output"] = {
            "name": generated.name,
            "bytes": len(source),
            "sha256": hashlib.sha256(source).hexdigest(),
            "encoding": "CP932 / encoding_rs SHIFT_JIS output",
            "encoding_validation": enc_status,
            "line_count": source.count(b"\n") + int(bool(source) and not source.endswith(b"\n")),
        }
        log_text = (stage / "decode.log").read_text(errors="replace")
        if result.returncode == 0 and source and enc_status == "valid" and "not implemented" not in log_text.lower():
            report["status"] = "raw_decode_complete"
        elif result.returncode == 0:
            report["status"] = "decoder_output_incomplete"
    (stage / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if verify_repeat:
        previous = json.loads((output / "report.json").read_text(encoding="utf-8"))
        if report["status"] != "raw_decode_complete" or previous["status"] != "raw_decode_complete":
            raise RuntimeError(f"Repeat decode did not complete; retained run: {stage}")
        if previous["input_sha256"] != input_sha or previous["tool"]["patch_sha256"] != build["patch_sha256"]:
            raise RuntimeError(f"Repeat comparison has different input/tool provenance; retained run: {stage}")
        if (output / generated.name).read_bytes() != generated.read_bytes():
            raise RuntimeError(f"Repeated decode differs byte-for-byte; retained run: {stage}")
        summary = {"status": "repeat_identical", "sha256": report["output"]["sha256"], "output": str(output)}
        shutil.rmtree(stage)
        return summary
    stage.rename(output)
    summary = {"status": report["status"], "report": str(output / "report.json")}
    if report["status"] != "raw_decode_complete":
        raise RuntimeError(f"Decode incomplete; inspect {output / 'report.json'} and {output / 'decode.log'}")
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=ROOT / "work/extracted/2.335/start.ax")
    parser.add_argument("--output", type=Path, default=OUTPUT_ROOT / "2.335")
    parser.add_argument("--verify-repeat", action="store_true")
    args = parser.parse_args()
    try:
        result = decompile(args.input, args.output, args.verify_repeat)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"decompile failed: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
