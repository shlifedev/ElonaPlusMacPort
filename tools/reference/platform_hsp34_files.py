#!/usr/bin/env python3
"""Run tools/reference/platform_fixtures/*.hsp on the official HSP 3.4 x86 console
runtime (hsp3cl.exe) on home/windows and fetch stdout/stderr/exit codes and the
files each case created. Session 0 (no GUI needed)."""
import argparse
import json
from pathlib import Path

from platform_remote import ROOT, check_run_name, fetch_run, make_remote_dir, ps, put_files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", default="platform-files-01")
    args = parser.parse_args()
    run = check_run_name(args.run)
    staging = ROOT / "work/platform-reference-input" / run
    if staging.exists():
        raise SystemExit(f"staging exists: {staging}")
    staging.mkdir(parents=True)
    files = {}
    for source in sorted((ROOT / "tools/reference/platform_fixtures").glob("*.hsp")):
        target = staging / source.name
        target.write_bytes(source.read_bytes().decode("utf-8").encode("cp932"))
        files[source.name] = target
    files["Hsp34Compile.cs"] = ROOT / "tools/reference/Hsp34Compile.cs"
    files["Hsp34Run.cs"] = ROOT / "tools/reference/Hsp34Run.cs"
    files["platform_run_hsp34_files.ps1"] = ROOT / "tools/reference/platform_run_hsp34_files.ps1"
    make_remote_dir(run)
    put_files(run, files)
    try:
        print(ps(f"""
$s = [IO.File]::ReadAllText((Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\reference-build\\{run}\\input\\platform_run_hsp34_files.ps1'))
& ([scriptblock]::Create($s)) -RunName '{run}'
""", timeout=600))
    finally:
        local = fetch_run(run, ROOT / "work/windows/reference-build")
    report = json.loads((local / "report.json").read_text(encoding="utf-8-sig"))
    for case in report["cases"]:
        d = local / "cases" / case["name"]
        out = (d / "run.stdout").read_bytes().decode("cp932", "replace") if (d / "run.stdout").exists() else ""
        err = (d / "run.stderr").read_bytes().decode("cp932", "replace") if (d / "run.stderr").exists() else ""
        print(f"== {case['name']} compile={case['compileExit']} run={case['runExit']}\n{out}{err}files={case['caseFiles']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
