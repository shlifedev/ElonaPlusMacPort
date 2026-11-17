#!/usr/bin/env python3
"""Export only reviewed small fixtures for the official Windows HSP 3.4 runtime."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "work/windows/reference-fixtures/input")
    parser.add_argument("--game-dir", type=Path, default=ROOT / "elonaplus2.335")
    parser.add_argument("--extended-rng", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "work/windows"):
        parser.error("Output must be inside work/windows")
    plugin = (args.game_dir / "exrand.dll").read_bytes()
    plugin_hash = hashlib.sha256(plugin).hexdigest()
    if plugin_hash != "fb99c1443797646428c705740ae0a93aacfe9cd76e3341b0e24970fb061a0b23":
        parser.error("The exrand.dll is not the pinned original 2.335 plugin")
    output.mkdir(parents=True, exist_ok=False)
    semantics = load("native_semantics", ROOT / "tests/native/run_semantics.py")
    compatibility = load("native_compatibility", ROOT / "tests/native/run_compatibility.py")
    cases = {name: (source, expected) for name, source, expected in semantics.CASES + semantics.CP932_CASES}
    cases.update({name: (source, expected) for name, source, expected in compatibility.REQUIRED_CASES})
    cases.update({name: (source, None) for name, source in compatibility.OBSERVATIONS})
    cases["exrand_seeded_rng"] = (
        '#uselib "exrand.dll"\n'
        '#func exrand_randomize "_exrand_randomize@16" int, int, int, int\n'
        '#func exrand_rnd "_exrand_rnd@16" var, int, int, int\n'
        'exrand_randomize 12345,0,0,0\nrepeat 10\n'
        'exrand_rnd value,32768,0,0\nmes str(value)\nloop\nend\n', None)
    if args.extended_rng:
        for seed in [0, 1, -1, -2147483647]:
            name = "rng_seed_" + str(seed).replace("-", "minus_")
            cases[name] = (f'randomize {seed}\nrepeat 5\nmes str(rnd(32768))\nloop\nend\n', None)
        for limit in [1, 2, 7, 100000, -7, 0]:
            name = "rng_range_" + str(limit).replace("-", "minus_")
            cases[name] = (f'randomize 12345\nrepeat 5\nmes str(rnd({limit}))\nloop\nend\n', None)
    rows = []
    for name, (source, expected) in cases.items():
        raw = source.encode("cp932")
        path = output / (name + ".hsp")
        path.write_bytes(raw)
        rows.append({"name": name, "source": path.name,
                     "sourceSha256": hashlib.sha256(raw).hexdigest(),
                     "expectedHex": expected.hex() if expected is not None else None,
                     "plugin": "exrand.dll" if name == "exrand_seeded_rng" else None})
    manifest = {"purpose": "Reviewed small HSP semantics fixtures only; never the original game",
                "sourceEncoding": "CP932", "fixtures": rows, "exrandSha256": plugin_hash,
                "originSha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                 for path in (ROOT / "tests/native/run_semantics.py", ROOT / "tests/native/run_compatibility.py")}}
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=True, indent=2) + "\n")
    (output / "exrand.dll").write_bytes(plugin)
    print(f"Wrote {len(rows)} fixtures to {output}")


if __name__ == "__main__":
    main()
