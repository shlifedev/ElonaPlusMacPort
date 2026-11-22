#!/usr/bin/env python3
"""Separate snapshot (not scripts/windows_stage.py) of the integrated host sources
for a Windows x64 headless build of elona_gui_host plus the host_regression
fixtures (non-ASCII and ASCII path variants) and an optional bounded game stage.
Writes only %USERPROFILE%\\ElonaPlusNative-2.335\\reference-build\\platform-host-*."""
import argparse
import hashlib
import json
import subprocess
import sys
import zipfile
from pathlib import Path

from platform_remote import REMOTE_BASE, ROOT, check_run_name, make_remote_dir, ps, put_files, sftp

from windows_snapshot import native_sources

FIXED_TIME = (2024, 1, 1, 0, 0, 0)


def add(zf, path: Path, arcname: str):
    info = zipfile.ZipInfo(arcname, FIXED_TIME)
    info.compress_type = zipfile.ZIP_DEFLATED
    zf.writestr(info, path.read_bytes())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--run", default="platform-host-01")
    ap.add_argument("--game", action="store_true")
    ap.add_argument("--source", type=Path, default=ROOT,
                    help="source tree to snapshot (repo root, or a private patched snapshot)")
    args = ap.parse_args()
    run = check_run_name(args.run)
    if not run.startswith("platform-host-"):
        raise SystemExit("run must start with platform-host-")
    staging = ROOT / "work/platform-reference-input" / run
    if staging.exists():
        raise SystemExit(f"staging exists: {staging}")
    staging.mkdir(parents=True)
    export = staging / "host-export"
    subprocess.check_call([sys.executable, str(ROOT / "scripts/reference/host_regression.py"), "--export", str(export)])
    archive = staging / "host-src.zip"
    src_root = args.source.resolve()
    files = list(native_sources(src_root))
    for rel in (".cache/upstream/OpenHSP/src/hsp3", ".cache/upstream/OpenHSP/src/hspcmp", ".cache/upstream/OpenHSP/common"):
        files.extend(p for p in sorted((src_root / rel).rglob("*")) if p.is_file() and "__pycache__" not in p.parts)
    files.extend(src_root / rel for rel in ("CMakeLists.txt", "CMakePresets.json", ".cache/downloads/zlib-1.3.1.tar.gz"))
    manifest = {}
    with zipfile.ZipFile(archive, "w") as zf:
        for p in files:
            rel = p.relative_to(src_root).as_posix()
            add(zf, p, rel)
            manifest[rel] = hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(export.rglob("*")):
            if p.is_file():
                add(zf, p, "host-export/" + p.relative_to(export).as_posix())
    (staging / "snapshot-manifest.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")
    upload = {"host-src.zip": archive, "host_windows_x64.ps1": ROOT / "scripts/reference/host_windows_x64.ps1"}
    if args.game:
        upload["start.ax"] = ROOT / "work/normalized/2.335/start.ax"
        upload["startup_english.script"] = ROOT / "tests/game/startup_english.script"
        upload["scene1.hsp"] = ROOT / "work/extracted/2.335/scene1.hsp"
        upload["scene2.hsp"] = ROOT / "work/extracted/2.335/scene2.hsp"
    make_remote_dir(run)
    put_files(run, upload)
    dest = ROOT / "work/windows/reference-build" / run
    dest.mkdir(parents=True, exist_ok=True)
    try:
        out = ps(f"""
$s = [IO.File]::ReadAllText((Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\reference-build\\{run}\\input\\host_windows_x64.ps1'))
& ([scriptblock]::Create($s)) -RunName '{run}' {'-Game' if args.game else ''}
""", timeout=3600)
        print(out[-6000:])
    finally:
        sftp(f'lcd "{dest}"\n-get {REMOTE_BASE}/{run}/report.json\n-get {REMOTE_BASE}/{run}/build.log\n'
             f'-get -R {REMOTE_BASE}/{run}/game\n', timeout=900)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
