#!/usr/bin/env python3
"""Stage the platform layer + pinned OpenHSP sources to home/windows, build them
as native x64 with MSVC/Ninja, run ctest (encoding incl. full Win32 table,
GameFileSystem, adapter) and the VM end-to-end fixtures. Dedicated run dir only."""
import argparse
import json
import subprocess
import sys
import zipfile
from pathlib import Path

from platform_remote import ROOT, check_run_name, fetch_run, make_remote_dir, ps, put_files

FIXED_TIME = (2024, 1, 1, 0, 0, 0)   # past timestamp: avoids Ninja clock-skew loops


def add_tree(zf, base: Path, rel_root: str, patterns=("*",)):
    for path in sorted((base / rel_root).rglob("*")):
        if path.is_file() and "__pycache__" not in path.parts:
            info = zipfile.ZipInfo(str(path.relative_to(base)).replace("\\", "/"), FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, path.read_bytes())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", default="platform-x64-01")
    parser.add_argument("--table", default="work/windows/reference-build/platform-cp932-02/out")
    args = parser.parse_args()
    run = check_run_name(args.run)
    staging = ROOT / "work/platform-reference-input" / run
    if staging.exists():
        raise SystemExit(f"staging exists: {staging}")
    staging.mkdir(parents=True)
    export = staging / "e2e-export"
    subprocess.check_call([sys.executable, str(ROOT / "tests/platform/run_hsp_e2e.py"), "--export", str(export)])
    archive = staging / "platform-src.zip"
    with zipfile.ZipFile(archive, "w") as zf:
        for rel in ("src/platform", "tests/platform", "cmake"):
            add_tree(zf, ROOT, rel)
        for rel in ("src/legacy_logical_path.cpp", "src/legacy_logical_path.h", "src/legacy_rng.cpp", "src/legacy_rng.h"):
            info = zipfile.ZipInfo(rel, FIXED_TIME); info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, (ROOT / rel).read_bytes())
        for rel in (".cache/upstream/OpenHSP/src/hsp3", ".cache/upstream/OpenHSP/src/hspcmp", ".cache/upstream/OpenHSP/common"):
            add_tree(zf, ROOT, rel)
        table = ROOT / args.table
        for name in ("cp932-decode.tsv", "cp932-encode.tsv"):
            info = zipfile.ZipInfo(f"cp932-table/{name}", FIXED_TIME); info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, (table / name).read_bytes())
        for path in sorted(export.rglob("*")):
            if path.is_file():
                info = zipfile.ZipInfo("e2e-export/" + str(path.relative_to(export)), FIXED_TIME)
                info.flag_bits |= 0x800   # UTF-8 names
                info.compress_type = zipfile.ZIP_DEFLATED
                zf.writestr(info, path.read_bytes())
    make_remote_dir(run)
    put_files(run, {"platform-src.zip": archive, "platform_build_x64.ps1": ROOT / "tools/reference/platform_build_x64.ps1"})
    try:
        print(ps(f"""
$s = [IO.File]::ReadAllText((Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\reference-build\\{run}\\input\\platform_build_x64.ps1'))
& ([scriptblock]::Create($s)) -RunName '{run}'
""", timeout=1800))
    finally:
        local_parent = ROOT / "work/windows/reference-build"
        # fetch only logs/report (not the build tree)
        from platform_remote import sftp, REMOTE_BASE
        dest = local_parent / run
        dest.mkdir(parents=True, exist_ok=True)
        sftp(f'lcd "{dest}"\nget {REMOTE_BASE}/{run}/report.json\nget {REMOTE_BASE}/{run}/build.log\n-get -R {REMOTE_BASE}/{run}/e2e\n', timeout=600)
    report = json.loads((dest / "report.json").read_text(encoding="utf-8-sig"))
    return 0 if report["buildAndCtestExit"] == 0 and report["allE2EOk"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
