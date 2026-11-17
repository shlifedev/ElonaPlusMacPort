#!/usr/bin/env python3
"""Report known portability gaps separately from the core VM smoke tests.

Exit 1 means an expected Elona-compatible semantic result was not obtained.
No game object, save, plugin, process execution, or filesystem HSP command runs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess


REQUIRED_CASES = [
    ("cp932_lowercase", 'mes getpath("、A.TXT",16)\nend\n', b"\x81\x41a.txt\n"),
    ("cp932_search_boundary", 's="、A"\nmes str(instr(s,0,"A"))\nend\n', b"2\n"),
    ("windows_logical_basename", 'mes getpath("C:\\\\game\\\\save\\\\character.sav",8)\nend\n', b"character.sav\n"),
]
OBSERVATIONS = [
    ("seeded_rng", 'randomize 12345\nrepeat 10\nmes str(rnd(32768))\nloop\nend\n'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("compiler", "runtime", "common", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    compiler, runtime, common, work = [getattr(args, n).resolve() for n in ("compiler", "runtime", "common", "work")]
    work.mkdir(parents=True, exist_ok=True)
    results = []
    for name, source, expected in REQUIRED_CASES + [(n, s, None) for n, s in OBSERVATIONS]:
        path = work / (name + ".hsp")
        path.write_bytes(source.encode("cp932"))
        obj = path.with_suffix(".ax")
        compile_result = subprocess.run([str(compiler), "-d", f"--compath={common}/", f"-o{obj}", str(path)],
                                        cwd=work, capture_output=True, timeout=15)
        (work / (name + ".compile.log")).write_bytes(compile_result.stdout + compile_result.stderr)
        row = {"case": name, "required": expected is not None}
        if compile_result.returncode:
            row.update(passed=False, stage="compile", exit=compile_result.returncode)
        else:
            run = subprocess.run([str(runtime), "--trusted-fixture", str(obj)], cwd=work, capture_output=True, timeout=15)
            (work / (name + ".stdout")).write_bytes(run.stdout)
            (work / (name + ".stderr")).write_bytes(run.stderr)
            row.update(exit=run.returncode, stdout_hex=run.stdout.hex(), stderr=run.stderr.decode("utf-8", "replace"),
                       ax_sha256=hashlib.sha256(obj.read_bytes()).hexdigest())
            if expected is not None:
                row.update(passed=run.returncode == 0 and not run.stderr and run.stdout.replace(b"\r\n", b"\n") == expected,
                           expected_hex=expected.hex())
            else:
                row.update(executed=run.returncode == 0 and not run.stderr)
        results.append(row)
    passed = all(row.get("passed", row.get("executed", False)) for row in results)
    summary = {"platform": platform.platform(), "machine": platform.machine(), "passed": passed,
               "results": results, "limitations": ["Reviewed fixtures only; no game execution", "Original Windows runtime is not the baseline yet", "RNG vector is an observation requiring cross-platform and original-runtime comparison"]}
    (work / "results.json").write_text(json.dumps(summary, indent=2) + "\n")
    for row in results:
        print(("PASS " if row.get("passed") else "FAIL " if row["required"] else "OBSERVE ") + row["case"])
        print(json.dumps(row))
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
