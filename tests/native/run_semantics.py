#!/usr/bin/env python3
"""Compile and execute reviewed semantic fixtures; does not launch game code."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess


CASES = [
    ("integers", 'a=2147483647\nb=-2147483647\nmes str(a)\nmes str(b)\nmes str(a & 255)\nmes str(-7 / 3)\nend\n', b"2147483647\n-2147483647\n255\n-2\n"),
    ("arrays", 'dim a,3,2\na(2,1)=42\nmes str(length(a))\nmes str(length2(a))\nmes str(varsize(a))\nmes str(a(2,1))\nend\n', b"3\n2\n24\n42\n"),
    ("control_flow", 'sum=0\nrepeat 6\nsum+=cnt\nloop\nif sum=15 : mes "ok"\ngosub *sub\nend\n*sub\nmes "sub"\nreturn\n', b"ok\nsub\n"),
    ("function", 'emit_value 21\nend\n#deffunc emit_value int n\nmes str(n*2)\nreturn\n', b"42\n"),
    ("double", 'a=1.25\nb=a*2.0\nmes strf("%.2f",b)\nend\n', b"2.50\n"),
    ("bytes", 'sdim s,8\npoke s,0,65\npoke s,1,66\nmes str(peek(s,0))\nmes str(wpeek(s,0))\nmes s\nend\n', b"65\n16961\nAB\n"),
    ("cp932_bytes", 's="AあB"\nmes str(strlen(s))\nmes strmid(s,1,2)\nend\n', b"4\n\x82\xa0\n"),
]

CP932_CASES = [
    ("noteadd_crlf", 'sdim s,64\nnotesel s\nnoteadd "A"\nnoteadd "あ"\nmes str(strlen(s))\nrepeat strlen(s)\nmes str(peek(s,cnt))\nloop\nmes str(noteinfo(0))\nend\n',
     b"7\n65\n13\n10\n130\n160\n13\n10\n2\n"),
    ("noteadd_unterminated", 's="tail"\nnotesel s\nnoteadd "next"\nmes str(strlen(s))\nrepeat strlen(s)\nmes str(peek(s,cnt))\nloop\nend\n',
     b"12\n116\n97\n105\n108\n13\n10\n110\n101\n120\n116\n13\n10\n"),
    ("noteadd_replace", 'sdim s,128\nnotesel s\nnoteadd "alpha"\nnoteadd "0"\nnoteadd "omega"\nnoteadd "1",1,1\nnotedel 0\nmes str(strlen(s))\nrepeat strlen(s)\nmes str(peek(s,cnt))\nloop\nend\n',
     b"10\n49\n13\n10\n111\n109\n101\n103\n97\n13\n10\n"),
    ("note_lf_is_data", 'sdim s,16\npoke s,0,65\npoke s,1,10\npoke s,2,66\nnotesel s\nmes str(noteinfo(0))\nnoteget first,0\nmes str(strlen(first))\nmes str(peek(first,1))\nend\n',
     b"1\n3\n10\n"),
    ("cp932_lowercase", 'mes getpath("、A.TXT",16)\nend\n', b"\x81\x41a.txt\n"),
    ("cp932_kana_lowercase", 'mes getpath("ソA.TXT",16)\nend\n', b"\x83\x5ca.txt\n"),
    ("cp932_search_boundary", 's="、A"\nmes str(instr(s,0,"A"))\nend\n', b"2\n"),
    ("cp932_kana_search_boundary", 's="ソ\\\\A"\nmes str(instr(s,0,"\\\\"))\nend\n', b"2\n"),
    ("windows_logical_basename", 'mes getpath("C:\\\\game\\\\save\\\\character.sav",8)\nend\n', b"character.sav\n"),
    ("cp932_logical_paths", 's="C:\\\\game.dir\\\\ソA.SAV"\nmes getpath(s,8)\nmes getpath(s,9)\nmes getpath(s,2)\nmes getpath(s,32)\nmes getpath(s,24)\nend\n',
     b"\x83\x5cA.SAV\n\x83\x5cA\n.SAV\nC:\\game.dir\\\n\x83\x5ca.sav\n"),
    ("logical_paths_relative", 'mes getpath("relative/ソ.TXT",8)\nmes getpath("C:character.sav",8)\nmes getpath("dir.name\\\\character",2)\nend\n', b"\x83\x5c.TXT\ncharacter.sav\n\n"),
]

RNG_CASES = [
    ("legacy_seeded_rng", 'randomize 12345\nrepeat 10\nmes str(rnd(32768))\nloop\nend\n',
     b"7584\n19164\n25795\n22125\n5828\n23405\n27477\n5413\n29072\n23404\n"),
    ("legacy_rng_seeds", ''.join(f'randomize {seed}\nrepeat 5\nmes str(rnd(32768))\nloop\n' for seed in [0, 1, -1, -2147483647]) + 'end\n',
     b"38\n7719\n21238\n2437\n8855\n41\n18467\n6334\n26500\n19169\n35\n29739\n3374\n11141\n31308\n41\n18467\n6334\n26500\n19169\n"),
    ("legacy_rng_ranges", ''.join(f'randomize 12345\nrepeat 5\nmes str(rnd({bound}))\nloop\n' for bound in [1, 2, 7, 100000, -7]) + 'end\n',
     b"0\n0\n0\n0\n0\n0\n0\n1\n1\n0\n3\n5\n0\n5\n4\n7584\n19164\n25795\n22125\n5828\n3\n5\n0\n5\n4\n"),
]


def execute(argv, cwd):
    return subprocess.run([str(x) for x in argv], cwd=cwd, capture_output=True, timeout=15)


def main():
    parser = argparse.ArgumentParser()
    for name in ["compiler", "runtime", "common", "work"]:
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--legacy-cp932", action="store_true", help="Require CP932 character-boundary semantics")
    parser.add_argument("--legacy-rng", action="store_true", help="Require the HSP 3.4 Windows reference RNG sequence")
    args = parser.parse_args()
    compiler, runtime, common, work = [getattr(args, n).resolve() for n in ["compiler", "runtime", "common", "work"]]
    work.mkdir(parents=True, exist_ok=True)
    results = []
    for name, source, expected in CASES + (CP932_CASES if args.legacy_cp932 else []) + (RNG_CASES if args.legacy_rng else []):
        path = work / f"{name}.hsp"
        path.write_bytes(source.encode("cp932"))
        obj = path.with_suffix(".ax")
        comp = execute([compiler, "-d", f"--compath={common}/", f"-o{obj}", path], work)
        (work / f"{name}.compile.log").write_bytes(comp.stdout + comp.stderr)
        if comp.returncode:
            results.append({"case": name, "passed": False, "stage": "compile", "exit": comp.returncode})
            continue
        run = execute([runtime, "--trusted-fixture", obj], work)
        (work / f"{name}.stdout").write_bytes(run.stdout)
        (work / f"{name}.stderr").write_bytes(run.stderr)
        results.append({"case": name, "passed": run.returncode == 0 and not run.stderr and run.stdout.replace(b"\r\n", b"\n") == expected,
                        "exit": run.returncode, "stdout_hex": run.stdout.hex(),
                        "expected_hex": expected.hex(), "stderr": run.stderr.decode("utf-8", "replace"),
                        "ax_sha256": hashlib.sha256(obj.read_bytes()).hexdigest()})
    # Rejection paths use a known generated object, never an untrusted program.
    sample = bytearray((work / "integers.ax").read_bytes())
    legacy = bytearray(sample)
    struct.pack_into("<I", legacy, 16, 96)
    malformed = bytearray(sample)
    struct.pack_into("<I", malformed, 20, 0x7fffffff)
    for name, contents, error in [("legacy_header_rejected", legacy, b"legacy AX header"),
                                  ("invalid_range_rejected", malformed, b"AX section exceeds")]:
        obj = work / f"{name}.ax"
        obj.write_bytes(contents)
        run = execute([runtime, "--trusted-fixture", obj], work)
        results.append({"case": name, "passed": run.returncode == 2 and not run.stdout and error in run.stderr,
                        "exit": run.returncode, "stdout_hex": run.stdout.hex(), "expected_hex": "",
                        "stderr": run.stderr.decode("utf-8", "replace"),
                        "ax_sha256": hashlib.sha256(obj.read_bytes()).hexdigest()})
    source = work / "unsupported_exec.hsp"
    source.write_text('exec "this-command-must-never-run"\nend\n', encoding="ascii")
    obj = source.with_suffix(".ax")
    comp = execute([compiler, f"--compath={common}/", f"-o{obj}", source], work)
    if comp.returncode:
        results.append({"case": "unsupported_exec", "passed": False, "stage": "compile"})
    else:
        run = execute([runtime, "--trusted-fixture", obj], work)
        results.append({"case": "unsupported_exec", "passed": run.returncode == 1 and not run.stdout and b"unsupported external command=2" in run.stderr,
                        "exit": run.returncode, "stdout_hex": run.stdout.hex(), "expected_hex": "",
                        "stderr": run.stderr.decode("utf-8", "replace"),
                        "ax_sha256": hashlib.sha256(obj.read_bytes()).hexdigest()})
    summary = {"platform": platform.platform(), "machine": platform.machine(), "legacy_cp932": args.legacy_cp932, "legacy_rng": args.legacy_rng, "results": results,
               "passed": all(r["passed"] for r in results),
               "limitations": ["Diagnostic host only; no game code executed", "Not compared against original Windows HSP runtime", "Not an untrusted-bytecode sandbox"]}
    (work / "results.json").write_text(json.dumps(summary, indent=2) + "\n")
    for row in results:
        print(("PASS " if row["passed"] else "FAIL ") + row["case"])
        if not row["passed"]:
            print(json.dumps(row))
    raise SystemExit(0 if summary["passed"] else 1)


if __name__ == "__main__":
    main()
