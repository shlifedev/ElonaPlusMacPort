#!/usr/bin/env python3
"""Read-only inspection of the 2.335 save arrays; never edits a save.

Field/layout evidence: recovered 2.335 source start.hsp (private): array
dimensions 23825-23839; player inventory range 29823-29836; saved lengths and
zRead/zWrite 307178-307322; coordinate use 611895; turn counter 610547;
calendar advancement 609680-609735. Array values are little-endian int32.
This is a diagnostic schema for 2.335, not a general save editor or migration.
"""
import argparse
import gzip
import hashlib
import json
import struct
from pathlib import Path

SIZES = {"gdata.s1": 4000, "cdata.s1": 114000, "sdata.s1": 273600,
         "inv.s1": 369600}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def payload(path):
    data = path.read_bytes()
    return gzip.decompress(data) if data[:2] == b"\x1f\x8b" else data


def manifest(root):
    return {p.relative_to(root).as_posix(): digest(p.read_bytes())
            for p in sorted(root.rglob("*")) if p.is_file()}


def arrays(root):
    out = {}
    for name, expected in SIZES.items():
        data = payload(root / name)
        if len(data) != expected:
            raise ValueError(f"{name}: expected {expected} decoded bytes, got {len(data)}")
        out[name] = struct.unpack(f"<{len(data) // 4}i", data)
    return out


def inspect(root):
    a = arrays(root)
    g, c, inv = a["gdata.s1"], a["cdata.s1"], a["inv.s1"]
    player_rows = [inv[i * 70:(i + 1) * 70] for i in range(200)]
    return {
        "path": str(root), "file_count": len(manifest(root)),
        "name": (root / "cdatan.s1").read_bytes().split(b"\r\n", 1)[0].decode("cp932"),
        "header": (root / "header.txt").read_bytes().split(b"\0", 1)[0].decode("cp932"),
        "player": {"x": c[1], "y": c[2], "level": c[38], "turn_counter_cdata26": c[26]},
        "location": {"gdata20": g[20], "gdata22": g[22]},
        "calendar": dict(zip(("year", "month", "day", "hour", "minute", "second"), g[10:16])),
        "player_inventory_active_rows": [i for i, row in enumerate(player_rows) if row[0] > 0],
        "player_inventory_payload_sha256": digest(payload(root / "inv.s1")[:200 * 70 * 4]),
        "arrays": {n: {"decoded_bytes": len(payload(root / n)),
                        "payload_sha256": digest(payload(root / n))} for n in SIZES},
    }


def compare(before, after):
    aa, bb = arrays(before), arrays(after)
    left, right = inspect(before), inspect(after)
    names = sorted(set(manifest(before)) | set(manifest(after)))
    files = {}
    for name in names:
        pa, pb = before / name, after / name
        if not pa.exists() or not pb.exists():
            files[name] = {"exists_before": pa.exists(), "exists_after": pb.exists()}
            continue
        da, db = payload(pa), payload(pb)
        row = {"compressed_identical": pa.read_bytes() == pb.read_bytes(),
               "payload_identical": da == db,
               "before_bytes": len(da), "after_bytes": len(db)}
        if name in SIZES:
            row["int32_changes"] = [{"index": i, "before": x, "after": y}
                                     for i, (x, y) in enumerate(zip(aa[name], bb[name])) if x != y]
        files[name] = row
    invariants = {
        "name": left["name"] == right["name"],
        "header": left["header"] == right["header"],
        "coordinates": all(left["player"][n] == right["player"][n] for n in ("x", "y")),
        "level": left["player"]["level"] == right["player"]["level"],
        "location": left["location"] == right["location"],
        "player_inventory_all_fields": left["player_inventory_payload_sha256"] == right["player_inventory_payload_sha256"],
        "all_saved_inventory_fields": files["inv.s1"]["payload_identical"],
        "all_saved_skill_fields": files["sdata.s1"]["payload_identical"],
    }
    return {"before": left, "after": right, "invariants": invariants,
            "invariants_passed": all(invariants.values()),
            "turn_counter_delta": right["player"]["turn_counter_cdata26"] - left["player"]["turn_counter_cdata26"],
            "files": files}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("save", type=Path)
    ap.add_argument("--compare", type=Path)
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()
    result = compare(args.save, args.compare) if args.compare else inspect(args.save)
    encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
    else:
        print(encoded, end="")
    return int(args.compare is not None and not result["invariants_passed"])


if __name__ == "__main__":
    raise SystemExit(main())
