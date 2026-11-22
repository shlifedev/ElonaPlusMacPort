#!/usr/bin/env python3
"""Run tests/gui fixtures (+ observation variants) on the official HSP 3.4 x86 GUI
runtime in the Windows interactive session, fetch logs/BMPs/captures, verify.

Remote: %USERPROFILE%\\ElonaPlusNative-2.335\\reference-build\\<gui-run>\\ only.
Local:  work/windows/reference-build/<gui-run>/ + verify.json (gui_verify.py).
"""
import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

from platform_remote import ROOT, check_run_name, fetch_run, make_remote_dir, ps, put_files

TESTS = ROOT / "tests/gui"
OBS = ROOT / "scripts/reference/gui_fixtures"
# name, mode, source, timeout_ms
CASES = [
    ("pixel_roundtrip", "exit", TESTS / "pixel_roundtrip.hsp", 15000),
    ("color_key", "exit", TESTS / "color_key.hsp", 15000),
    ("bitmap_export", "exit", TESTS / "bitmap_export.hsp", 15000),
    ("input_control", "input", TESTS / "input_control.hsp", 15000),
    ("gui_pixel_dump", "capture", OBS / "gui_pixel_dump.hsp", 15000),
    ("gui_colorkey_dump", "capture", OBS / "gui_colorkey_dump.hsp", 15000),
    ("gui_pattern_dump", "capture", OBS / "gui_pattern_dump.hsp", 15000),
]
# Diagnostic suite: why window captures of the canvas come back white.
DIAG = [
    ("gui_cap_plain", "capture", OBS / "gui_cap_plain.hsp", 15000),
    ("gui_cap_redraw", "capture", OBS / "gui_cap_redraw.hsp", 15000),
    ("gui_cap_large", "capture", OBS / "gui_cap_large.hsp", 15000),
    ("gui_cap_large_bmpsave", "capture", OBS / "gui_cap_large_bmpsave.hsp", 15000),
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", default="gui-fixtures-01")
    parser.add_argument("--suite", choices=("fixtures", "diag"), default="fixtures")
    args = parser.parse_args()
    run = check_run_name(args.run)
    staging = ROOT / "work/gui-reference-input" / run
    if staging.exists():
        raise SystemExit(f"staging exists: {staging}")
    staging.mkdir(parents=True)
    files = {}
    rows = []
    for name, mode, source, timeout in (CASES if args.suite == "fixtures" else DIAG):
        encoded = source.read_bytes().decode("utf-8").encode("cp932")
        target = staging / (name + ".hsp")
        target.write_bytes(encoded)
        files[name + ".hsp"] = target
        rows.append("\t".join([name, mode, name + ".hsp", hashlib.sha256(encoded).hexdigest(), str(timeout)]))
    (staging / "cases.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")
    files["cases.tsv"] = staging / "cases.tsv"
    files["Hsp34Compile.cs"] = ROOT / "scripts/reference/Hsp34Compile.cs"
    files["GuiFixtureProbe.cs"] = ROOT / "scripts/reference/GuiFixtureProbe.cs"
    files["gui_run_fixtures.ps1"] = ROOT / "scripts/reference/gui_run_fixtures.ps1"
    make_remote_dir(run)
    put_files(run, files)
    try:
        out = ps(f"""
$script = [IO.File]::ReadAllText((Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\reference-build\\{run}\\input\\gui_run_fixtures.ps1'))
& ([scriptblock]::Create($script)) -RunName '{run}'
""", timeout=600)
        print(out)
    finally:
        local = fetch_run(run, ROOT / "work/windows/reference-build")
        print(f"fetched {local}")
    if args.suite == "diag":
        return 0
    return subprocess.call([sys.executable, str(ROOT / "scripts/reference/gui_verify.py"), str(local)])


if __name__ == "__main__":
    raise SystemExit(main())
