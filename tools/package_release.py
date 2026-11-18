#!/usr/bin/env python3
"""Build the distributable ElonaPlus package around the Tauri launcher.

  mac:     ElonaPlus.app (ad-hoc signed) in ElonaPlus-<version>-macos-arm64.dmg
  windows: ElonaPlus/ElonaPlus.exe + elona_gui_host.exe in ElonaPlus-<version>-windows-x64.zip

Bundled: launcher, native host, prebuilt game code from packaging/game-data.json
(SHA-256 checked), Korean catalogs, SoundFont, notices. Never bundled: the
game's graphics, sound or maps (read from the user's own 2.335 folder).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "launcher"
STAGE = LAUNCHER / "stage"
TAURI_CLI = "@tauri-apps/cli@2.12.4"
SOUNDFONT = ROOT / ".cache/audio/GeneralUser-GS/GeneralUser-GS.sf2"
FRAMEWORKS = [ROOT / ".cache/macos-gui/SDL3-3.4.20/SDL3.framework",
              ROOT / ".cache/macos-gui/SDL3_ttf-3.2.2/SDL3_ttf.framework"]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(*args, cwd=None) -> str:
    return subprocess.run([str(a) for a in args], cwd=cwd, check=True, capture_output=True, encoding="utf-8").stdout


def fresh(path: Path) -> Path:
    shutil.rmtree(path, ignore_errors=True)
    path.mkdir(parents=True)
    return path


def stage_data(dest: Path, game_data: Path | None, version: str) -> None:
    manifest = json.loads((ROOT / "packaging/game-data.json").read_text(encoding="utf-8"))
    (dest / "game").mkdir(parents=True)
    for f in manifest["files"]:
        src = game_data / f["name"] if game_data else ROOT / f["source"]
        if not src.is_file() or sha256(src) != f["sha256"]:
            raise SystemExit(f"missing or unexpected game data: {src}")
        shutil.copy2(src, dest / "game" / f["name"])
    (dest / "localization").mkdir()
    for name in ("runtime.tsv", "draft-display.tsv"):
        shutil.copy2(ROOT / "localization/ko-KR" / name, dest / "localization" / name)
    shutil.copy2(SOUNDFONT, dest / "GeneralUser-GS.sf2")


def add_notices(dest: Path, version: str) -> None:
    shutil.copytree(ROOT / "licenses", dest / "licenses", dirs_exist_ok=True)
    shutil.copy2(ROOT / "THIRD_PARTY_NOTICES.md", dest / "THIRD_PARTY_NOTICES.md")
    (dest / "licenses/rust-crates.txt").write_text(crate_licenses(), encoding="utf-8")
    readme = (ROOT / "packaging/README.txt").read_text(encoding="utf-8").replace("{version}", version)
    (dest / "README.txt").write_text(readme, encoding="utf-8")


def crate_licenses() -> str:
    """License texts of every crate linked into the launcher (from the cargo registry sources)."""
    meta = json.loads(run("cargo", "metadata", "--format-version", "1", "--locked", cwd=LAUNCHER))
    out = []
    for pkg in sorted(meta["packages"], key=lambda p: (p["name"], p["version"])):
        if pkg["source"] is None:
            continue
        folder = Path(pkg["manifest_path"]).parent
        texts = [p for p in sorted(folder.iterdir()) if p.is_file() and p.name.upper().startswith(("LICENSE", "COPYING", "NOTICE"))]
        out.append(f"==== {pkg['name']} {pkg['version']} ({pkg.get('license') or 'see files'}) ====\n")
        out += [t.read_text(encoding="utf-8", errors="replace") + "\n" for t in texts]
    return "\n".join(out)


def tauri_build(version: str, *extra: str) -> None:
    STAGE.mkdir(exist_ok=True)
    (STAGE / "version.json").write_text(json.dumps({"version": version}))
    npx = shutil.which("npx") or "npx"
    subprocess.run([npx, "--yes", TAURI_CLI, "build", "--config", str(STAGE / "version.json"), *extra],
                   cwd=LAUNCHER, check=True)


def smoke_test(host: Path) -> None:
    """Fail unless the packaged host loads its bundled libraries (it exits 2 on an unknown option)."""
    r = subprocess.run([str(host), "--package-smoke-test"], capture_output=True, text=True, timeout=60)
    if r.returncode != 2 or "Usage:" not in r.stdout + r.stderr:
        raise SystemExit(f"packaged host failed to start ({r.returncode}):\n{r.stderr}")


def package_mac(build: Path, out: Path, game_data, version: str) -> Path:
    fresh(STAGE)
    triple = next(l.split()[1] for l in run("rustc", "-vV").splitlines() if l.startswith("host:"))
    host = STAGE / f"elona_gui_host-{triple}"
    shutil.copy2(build / "elona_gui_host", host)
    for line in run("otool", "-l", host).splitlines():
        line = line.strip()
        if line.startswith("path ") and ".cache" in line:
            run("install_name_tool", "-delete_rpath", line.split()[1], host)
    run("install_name_tool", "-add_rpath", "@executable_path/../Frameworks", host)
    for fw in FRAMEWORKS:
        shutil.copytree(fw, STAGE / "Frameworks" / fw.name, symlinks=True)
    stage_data(STAGE / "data", game_data, version)
    add_notices(STAGE / "data", version)
    tauri_build(version, "--bundles", "app")

    app = LAUNCHER / "target/release/bundle/macos/ElonaPlus.app"
    run("codesign", "--verify", "--deep", "--strict", app)
    smoke_test(app / "Contents/MacOS/elona_gui_host")
    dmg_root = fresh(out / "dmg")
    shutil.copytree(app, dmg_root / app.name, symlinks=True)
    shutil.copy2(STAGE / "data/README.txt", dmg_root / "README.txt")
    os.symlink("/Applications", dmg_root / "Applications")
    dmg = out / f"ElonaPlus-{version}-macos-arm64.dmg"
    dmg.unlink(missing_ok=True)
    run("hdiutil", "create", "-volname", "ElonaPlus", "-srcfolder", dmg_root, "-fs", "HFS+", "-format", "UDZO", dmg)
    return dmg


def package_windows(build: Path, out: Path, game_data, version: str, sdl_root: Path) -> Path:
    tauri_build(version, "--no-bundle")
    app = fresh(out / "zip/ElonaPlus")
    shutil.copy2(LAUNCHER / "target/release/ElonaPlus.exe", app / "ElonaPlus.exe")
    shutil.copy2(build / "elona_gui_host.exe", app / "elona_gui_host.exe")
    for dll in ("SDL3.dll", "SDL3_ttf.dll"):
        shutil.copy2(next(sdl_root.glob(f"*/lib/x64/{dll}")), app / dll)
    smoke_test(app / "elona_gui_host.exe")
    stage_data(app / "data", game_data, version)
    add_notices(app, version)
    archive = out / f"ElonaPlus-{version}-windows-x64.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for p in sorted(app.rglob("*")):
            if p.is_file():
                z.write(p, p.relative_to(app.parent).as_posix())
    return archive


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--platform", choices=("mac", "windows"), required=True)
    ap.add_argument("--build-dir", type=Path, required=True, help="CMake build dir with elona_gui_host")
    ap.add_argument("--game-data", type=Path, help="dir with packaging/game-data.json files (default: local work/)")
    ap.add_argument("--sdl-root", type=Path, help="windows: extracted SDL3/SDL3_ttf VC packages")
    ap.add_argument("--version", default="0.1.0")
    ap.add_argument("--out", type=Path, default=ROOT / "build/package")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if args.platform == "mac":
        result = package_mac(args.build_dir.resolve(), args.out.resolve(), args.game_data, args.version)
    else:
        if not args.sdl_root:
            ap.error("--sdl-root is required for windows")
        result = package_windows(args.build_dir.resolve(), args.out.resolve(), args.game_data, args.version, args.sdl_root)
    print(f"{result} {result.stat().st_size} bytes sha256={sha256(result)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
