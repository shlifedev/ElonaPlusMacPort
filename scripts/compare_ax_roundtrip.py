#!/usr/bin/env python3
"""Compare legacy AX token values/branch targets after resolving table indices.

This is a static comparison, not a game behavior or arbitrary-bytecode validator.
Storage-width and HSP single-expression optimization flags are counted separately.
"""
import argparse
from array import array
from bisect import bisect_left
from collections import Counter
import hashlib
from itertools import zip_longest
import json
from pathlib import Path
import struct

from elona_extract import inspect_ax

ROOT = Path(__file__).resolve().parents[1]


class Object:
    def __init__(self, path):
        self.data = path.read_bytes()
        self.info = inspect_ax(self.data)
        self.code = self.info["segments"]["code"]
        self.ds = self.info["segments"]["data"]["offset"]
        self.ds_end = self.ds + self.info["segments"]["data"]["bytes"]
        self.libraries = [(lib["name"], lib["flag"], lib["stored_handle"],
                           None if lib["flag"] == 1 else lib["clsid_raw"])
                          for lib in self.info["libraries"]]
        self.positions = array("I", (p for p, _, _, _, _ in self.tokens()))
        labels = self.info["segments"]["labels"]
        self.labels = [self.target(v * 2) for v, in struct.iter_unpack("<I", self.data[labels["offset"]:labels["offset"] + labels["bytes"]])]
        self.functions = {}
        self.parameters = {}
        self.declarations = {}
        occurrences = Counter()
        for f in self.info["functions"]:
            library = self.info["libraries"][f["library_index"]]["name"] if f["library_index"] is not None else None
            base_identity = (f["kind"], library, f["name"])
            # GetLastError is declared twice. Preserve each occurrence rather
            # than collapsing declarations or comparing unstable table IDs.
            occurrences[base_identity] += 1
            identity = base_identity + (occurrences[base_identity],)
            self.functions[f["index"]] = identity
        for f in self.info["functions"]:
            identity = self.functions[f["index"]]
            library = identity[1]
            for index, param in enumerate(f["parameters"]):
                self.parameters[f["first_parameter"] + index] = (identity, index)
            self.declarations[identity] = {"parameters": [(p["type"], self.functions[p["subid"]] if p["subid"] >= 0 else p["subid"], p["offset"]) for p in f["parameters"]],
                                           "stack_bytes": f["stack_bytes"], "function_flags": f["function_flags"]}
            if library is not None:
                self.declarations[identity]["subid"] = f["subid"]
                self.declarations[identity]["target_or_flags"] = f["target_or_flags"]
            else:
                self.declarations[identity]["target_token"] = self.labels[f["target_or_flags"]]
        self.strings = {}

    def target(self, offset):
        index = bisect_left(self.positions, offset)
        if index == len(self.positions) or self.positions[index] != offset:
            raise ValueError(f"Target does not point at a token: {offset}")
        return index

    def tokens(self):
        start = self.code["offset"]
        pos = 0
        while pos < self.code["bytes"]:
            if pos + 4 > self.code["bytes"]:
                raise ValueError("Truncated token at CS byte " + str(pos))
            kind, flags = self.data[start + pos:start + pos + 2]
            if flags & 0x0f:
                raise ValueError("Unsupported token flags at CS byte " + str(pos))
            size = 6 if flags & 128 else 4
            if pos + size > self.code["bytes"]:
                raise ValueError("Truncated token value at CS byte " + str(pos))
            value = struct.unpack_from("<i" if size == 6 else "<H", self.data, start + pos + 2)[0]
            extra = None
            if kind == 11 and flags & 32:
                if pos + size + 2 > self.code["bytes"]:
                    raise ValueError("Truncated branch displacement at CS byte " + str(pos))
                extra = struct.unpack_from("<H", self.data, start + pos + size)[0]
                size += 2
            yield pos, kind, flags, value, extra
            pos += size

    def normalized(self):
        for pos, kind, flags, value, extra in self.tokens():
            original = value
            if kind == 2:
                if value not in self.strings:
                    if value < 0 or self.ds + value >= self.ds_end:
                        raise ValueError("String offset outside DS")
                    end = self.data.find(b"\0", self.ds + value, self.ds_end)
                    if end < 0:
                        raise ValueError("Unterminated string within DS")
                    self.strings[value] = self.data[self.ds + value:end]
                value = self.strings[value]
            elif kind == 3:
                if value < 0 or self.ds + value + 8 > self.ds_end:
                    raise ValueError("Double offset outside DS")
                value = self.data[self.ds + value:self.ds + value + 8]
            elif kind == 5:
                value = self.parameters[value]
            elif kind == 7:
                value = self.labels[value]
            elif kind in (12, 16):
                value = self.functions[value]
            elif kind == 17 and value >= 4096:
                value = self.functions[value - 4096]
            # HSP jumps relative to the end of the if/else opcode + extra word.
            target = self.target(pos + (8 if flags & 128 else 6) + extra * 2) if extra is not None else None
            yield (kind, flags & 0x60, value, target), {"csByteOffset": pos, "kind": kind, "flags": flags, "rawValue": original, "targetToken": target}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("rebuilt", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.is_symlink() or args.output.parent.is_symlink():
        parser.error("Report path and parent must not be symlinks")
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "work/reports") or output in (args.original.resolve(), args.rebuilt.resolve()):
        parser.error("Report must be a separate file inside work/reports")
    original, rebuilt = Object(args.original), Object(args.rebuilt)
    mismatches = 0
    examples = []
    flag_changes = Counter()
    width_changes = []
    for index, pair in enumerate(zip_longest(original.normalized(), rebuilt.normalized())):
        left, right = pair
        if left is None or right is None or left[0] != right[0]:
            mismatches += 1
            if len(examples) < 30:
                examples.append({"token": index, "original": left[1] if left else None, "rebuilt": right[1] if right else None})
        if left and right and left[1]["flags"] != right[1]["flags"]:
            flag_changes[f'{left[1]["flags"]:02x}->{right[1]["flags"]:02x}'] += 1
            if (left[1]["flags"] ^ right[1]["flags"]) & 0x80:
                width_changes.append({"token": index, "original": left[1], "rebuilt": right[1]})
    declarations = []
    for identity in sorted(original.declarations.keys() | rebuilt.declarations.keys(), key=repr):
        if original.declarations.get(identity) != rebuilt.declarations.get(identity):
            declarations.append({"identity": identity, "original": original.declarations.get(identity), "rebuilt": rebuilt.declarations.get(identity)})
    report = {"originalSha256": original.info["sha256"], "rebuiltSha256": rebuilt.info["sha256"],
              "toolSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "originalTokens": len(original.positions), "rebuiltTokens": len(rebuilt.positions),
              "originalCodeBytes": original.code["bytes"], "rebuiltCodeBytes": rebuilt.code["bytes"],
              "normalizedTokenMismatches": mismatches, "examples": examples, "flagChanges": dict(flag_changes),
              "storageWidthChanges": width_changes,
              "declarationMismatches": len(declarations), "declarations": declarations,
              "librariesMatch": original.libraries == rebuilt.libraries,
              "gameBehaviorVerified": False,
              "limitations": ["Token values/line+argument flags and actual branch targets only; no execution",
                              "Storage-width flag 0x80 and single-expression optimization flag 0x10 compared separately",
                              "Header/debug layout and unused literal bytes need not match",
                              "Unused clsid field for ordinary DLL libraries (flag 1) is not compared"]}
    contents = (json.dumps(report, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        if not output.is_file() or output.read_bytes() != contents:
            parser.error("Existing report differs; choose a new report path")
    else:
        # Exclusive create also refuses a symlink that appears after validation.
        with output.open("xb") as stream:
            stream.write(contents)
    print(json.dumps({k: report[k] for k in ("originalTokens", "rebuiltTokens", "normalizedTokenMismatches", "declarationMismatches", "flagChanges")}))
    return int(mismatches != 0 or bool(declarations) or not report["librariesMatch"])


if __name__ == "__main__":
    raise SystemExit(main())
