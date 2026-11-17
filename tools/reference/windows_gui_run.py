#!/usr/bin/env python3
"""Native Windows x64 GUI verification of elona_gui_host (SDL3 + SDL3_ttf).

1. Private source snapshot (work/orchestration/windows-gui/<run>/snapshot): the
   current repo sources; the Windows Unicode host patch is applied to the
   snapshot only if the shared sources do not contain it yet.
2. Stage snapshot, pinned SDL3/SDL3_ttf packages (windows_gui_deps.py), CP932
   fixtures, the normalized original AX, DPM members and deterministic scripts
   to %USERPROFILE%\\ElonaPlusNative-2.335\\reference-build\\<run>\\input.
3. Run tools/reference/windows_gui_build.ps1 remotely (build, session-0
   headless, session-1 real windows via a temporary Interactive/Limited task).
4. Fetch evidence to work/windows/reference-build/<run>/ and verify with
   tools/reference/windows_gui_verify.py.
"""
import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

from platform_remote import REMOTE_BASE, ROOT, check_run_name, make_remote_dir, ps, put_files, sftp

FIXED_TIME = (2024, 1, 1, 0, 0, 0)
PATCH = ROOT / "work/orchestration/windows-unicode-host.patch"
GAMES = [
    {"name": "startup", "mode": "headless", "script": "startup_english.script", "maxWaits": 3000, "timeoutMs": 240000},
    {"name": "newgame", "mode": "headless", "script": "newgame_english.script", "maxWaits": 5000, "timeoutMs": 900000},
    {"name": "load", "mode": "headless", "script": "load_continue.script", "maxWaits": 1100, "timeoutMs": 300000,
     "reuseUserFrom": "newgame"},
    {"name": "window-startup", "mode": "window", "script": "startup_english.script", "maxWaits": 1000000,
     "timeoutMs": 45000, "captureDelayMs": 15000, "captureReadyScreenshot": "f0400.bmp"},
]


def load_fixture_table():
    spec = importlib.util.spec_from_file_location("run_gui_fixtures", ROOT / "tests/graphics/run_gui_fixtures.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def add(zf, data: bytes, arc: str):
    info = zipfile.ZipInfo(arc, FIXED_TIME)
    info.compress_type = zipfile.ZIP_DEFLATED
    zf.writestr(info, data)


def make_snapshot(dest: Path) -> dict:
    for rel in ("src", "cmake", "tests/audio", ".cache/audio", ".cache/upstream/OpenHSP/src/hsp3", ".cache/upstream/OpenHSP/src/hspcmp",
                ".cache/upstream/OpenHSP/common"):
        shutil.copytree(ROOT / rel, dest / rel, ignore=shutil.ignore_patterns("__pycache__"))
    for rel in ("CMakeLists.txt", "CMakePresets.json", ".cache/downloads/zlib-1.3.1.tar.gz"):
        (dest / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / rel, dest / rel)
    info = {"unicode_patch_in_shared_sources": "wmain" in (ROOT / "src/app/main.cpp").read_text()}
    if not info["unicode_patch_in_shared_sources"]:
        r = subprocess.run(["git", "apply", "--whitespace=nowarn", str(PATCH)], cwd=dest, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f"Unicode patch does not apply to the snapshot:\n{r.stderr}")
        info["unicode_patch_applied_to_snapshot"] = hashlib.sha256(PATCH.read_bytes()).hexdigest()
    info["files"] = {p.relative_to(dest).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in sorted(dest.rglob("*")) if p.is_file() and p.parts[len(dest.parts)] in ("src", "cmake")}
    return info


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--run", default="gui-native-01")
    ap.add_argument("--snapshot-from", help="reuse a prior immutable private source snapshot")
    args = ap.parse_args()
    run = check_run_name(args.run)
    if not run.startswith("gui-native-"):
        raise SystemExit("run must start with gui-native-")
    stage = ROOT / "work/orchestration/windows-gui" / run
    if stage.exists():
        raise SystemExit(f"exists: {stage}")
    stage.mkdir(parents=True)
    if args.snapshot_from:
        old = check_run_name(args.snapshot_from)
        prior = ROOT / "work/orchestration/windows-gui" / old
        if not old.startswith("gui-native-") or not (prior / "snapshot-info.json").is_file():
            raise SystemExit("snapshot source is not a prepared gui-native run")
        shutil.copytree(prior / "snapshot", stage / "snapshot")
        snap_info = json.loads((prior / "snapshot-info.json").read_text())
        snap_info["reused_snapshot_from"] = old
    else:
        snap_info = make_snapshot(stage / "snapshot")
    (stage / "snapshot-info.json").write_text(json.dumps(snap_info, indent=1), encoding="utf-8")
    with zipfile.ZipFile(stage / "snapshot.zip", "w") as zf:
        for p in sorted((stage / "snapshot").rglob("*")):
            if p.is_file():
                add(zf, p.read_bytes(), p.relative_to(stage / "snapshot").as_posix())
    mod = load_fixture_table()
    fixtures = []
    with zipfile.ZipFile(stage / "fixtures.zip", "w") as zf:
        for table, base in ((mod.FIXTURES, mod.GUI), (mod.REFERENCE_FIXTURES, mod.REFERENCE), (mod.LOCAL_FIXTURES, mod.LOCAL)):
            for name in table:
                add(zf, (base / f"{name}.hsp").read_bytes().decode("utf-8").encode("cp932"), f"{name}.hsp")
                script = mod.LOCAL / f"{name}.script"
                if script.exists():
                    add(zf, script.read_bytes(), f"{name}.script")
                fixtures.append({"name": name, "script": script.exists()})
    lock = json.loads((ROOT / "tools/reference/windows_gui_deps.lock.json").read_text())
    deps = [{"name": n, "sha256": v["sha256"]} for n, v in lock.items()]
    manifest = {"deps": deps, "sdl3_dir": "SDL3-3.4.20", "sdl3_ttf_dir": "SDL3_ttf-3.2.2", "fixtures": fixtures, "games": GAMES}
    (stage / "manifest.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")
    upload = {"manifest.json": stage / "manifest.json", "snapshot.zip": stage / "snapshot.zip",
              "fixtures.zip": stage / "fixtures.zip",
              "start.ax": ROOT / "work/normalized/2.335/start.ax",
              "scene1.hsp": ROOT / "work/extracted/2.335/scene1.hsp", "scene2.hsp": ROOT / "work/extracted/2.335/scene2.hsp",
              "WindowsGuiProbe.cs": ROOT / "tools/reference/WindowsGuiProbe.cs",
              "windows_gui_build.ps1": ROOT / "tools/reference/windows_gui_build.ps1"}
    for g in GAMES:
        upload[g["script"]] = ROOT / "tests/game" / g["script"]
    for d in deps:
        upload[d["name"]] = ROOT / ".cache/downloads/windows-gui" / d["name"]
    make_remote_dir(run)
    put_files(run, upload)
    dest = ROOT / "work/windows/reference-build" / run
    dest.mkdir(parents=True, exist_ok=True)
    try:
        out = ps(f"""
$s = [IO.File]::ReadAllText((Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\reference-build\\{run}\\input\\windows_gui_build.ps1'))
& ([scriptblock]::Create($s)) -RunName '{run}'
""", timeout=5400)
        (dest / "remote-stdout.txt").write_text(out, encoding="utf-8")
    finally:
        sftp(f'lcd "{dest}"\n-get {REMOTE_BASE}/{run}/report.json\n-get {REMOTE_BASE}/{run}/build.log\n'
             f'-get {REMOTE_BASE}/{run}/window-plan.tsv\n-get -R {REMOTE_BASE}/{run}/headless\n'
             f'-get -R {REMOTE_BASE}/{run}/window\n-get -R {REMOTE_BASE}/{run}/window-results\n'
             f'-get -R {REMOTE_BASE}/{run}/evidence\n', timeout=1800)
    return subprocess.call([sys.executable, str(ROOT / "tools/reference/windows_gui_verify.py"), str(dest)])


if __name__ == "__main__":
    raise SystemExit(main())
