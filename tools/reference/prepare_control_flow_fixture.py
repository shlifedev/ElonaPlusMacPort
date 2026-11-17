#!/usr/bin/env python3
"""Write an original synthetic legacy-double-else reproducer, without game code."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--roundtrip", action="store_true")
args = parser.parse_args()
output = ROOT / "work/windows/control-flow-fixture" / ("input-roundtrip" if args.roundtrip else "input")
output.mkdir(parents=True, exist_ok=False)
cases = {
    "double_else": 'repeat 2\nif cnt==0 {\nmes "true"\n}\nelse {\nmes "false"\n}\nelse {\nmes "extra"\n}\nloop\nend\n',
    "stop_ghost": 'if 0 {\nstop\n}\nmes "done"\nend\n',
}
if args.roundtrip:
    cases = {}
    for name in ["double_else", "stop_ghost"]:
        raw = (ROOT / "work/windows/control-flow-fixture/decode" / name / (name + ".hsp")).read_bytes().decode("ascii")
        cases[name + "_raw"] = raw
        if name == "double_else":
            target = '\t\t\tmes "extra"'
            if raw.count(target) != 1:
                raise ValueError("Unexpected double-else reproducer")
            fixed = raw.replace(target, '\t\t}\r\n\t\telse {\r\n' + target)
        else:
            target = '\t\tgoto *label_0001\r\n'
            if raw.count(target) != 1:
                raise ValueError("Unexpected stop reproducer")
            fixed = raw.replace(target, '')
        cases[name + "_fixed"] = fixed
rows = []
for name, source in cases.items():
    raw = source.encode("ascii")
    (output / (name + ".hsp")).write_bytes(raw)
    rows.append({"name": name, "source": name + ".hsp",
                 "sourceSha256": hashlib.sha256(raw).hexdigest(), "expectedHex": None, "plugin": None})
(output / "manifest.json").write_text(json.dumps({"purpose": "Small synthetic borscht control-flow reproducer",
                                                  "fixtures": rows}, indent=2) + "\n")
print(output)
