#!/usr/bin/env python3
"""Fetch and pin the private Windows GUI dependencies (official SDL3 / SDL3_ttf
MSVC development packages, same versions as the Mac build: SDL3 3.4.20,
SDL3_ttf 3.2.2) into .cache/downloads/windows-gui/, verify SHA-256, and stage
them to the dedicated remote folder reference-build/<run>/input.

No global install, PATH or environment change on either machine. The remote
consumer is tools/reference/windows_gui_build.ps1 (CMAKE_PREFIX_PATH per run).

SDL3: verified against the GitHub release asset digest published by libsdl-org.
SDL3_ttf 3.2.2: the release has no asset digest and the VC package is not
signed (only the source archives carry .sig), so the SHA-256 below was pinned at
first download (2026-11-18) and is re-checked on every use.
"""
import argparse
import hashlib
import json
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / ".cache/downloads/windows-gui"
DEPS = [
    {"name": "SDL3-devel-3.4.20-VC.zip",
     "url": "https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-devel-3.4.20-VC.zip",
     "sha256": "1ec3009655ee37b1f73e52b3b120acc5afd8abaf9532f3303b1bf1dfe91b5969",
     "sha256_source": "GitHub release asset digest (libsdl-org/SDL release-3.4.20, published 2026-11-03)",
     "license": "zlib (LICENSE.txt inside the package)"},
    {"name": "SDL3_ttf-devel-3.2.2-VC.zip",
     "url": "https://github.com/libsdl-org/SDL_ttf/releases/download/release-3.2.2/SDL3_ttf-devel-3.2.2-VC.zip",
     "sha256": None,   # filled from tools/reference/windows_gui_deps.lock.json
     "sha256_source": "pinned at first download 2026-11-18 (no upstream asset digest or signature for this package)",
     "license": "zlib (LICENSE.txt); bundled FreeType/HarfBuzz/PlutoSVG notices inside the package"},
]
LOCK = Path(__file__).with_name("windows_gui_deps.lock.json")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(pin_missing: bool):
    CACHE.mkdir(parents=True, exist_ok=True)
    lock = json.loads(LOCK.read_text()) if LOCK.exists() else {}
    out = []
    for dep in DEPS:
        expected = dep["sha256"] or lock.get(dep["name"], {}).get("sha256")
        path = CACHE / dep["name"]
        if not path.exists():
            tmp = path.with_suffix(".part")
            with urllib.request.urlopen(dep["url"]) as r, tmp.open("wb") as f:
                while True:
                    b = r.read(1 << 20)
                    if not b:
                        break
                    f.write(b)
            tmp.rename(path)
        actual = sha256(path)
        if expected is None:
            if not pin_missing:
                raise SystemExit(f"{dep['name']}: no pinned SHA-256; rerun with --pin to record {actual}")
            expected = actual
        if actual != expected:
            raise SystemExit(f"{dep['name']}: SHA-256 mismatch {actual} != {expected}")
        with zipfile.ZipFile(path) as z:
            notices = sorted(n for n in z.namelist() if any(k in n.upper() for k in ("LICENSE", "COPYING", "README")))
        out.append({**dep, "sha256": actual, "bytes": path.stat().st_size, "notices_in_package": notices})
        lock[dep["name"]] = {"sha256": actual, "url": dep["url"], "sha256_source": dep["sha256_source"]}
    LOCK.write_text(json.dumps(lock, indent=1) + "\n")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pin", action="store_true", help="record SHA-256 for deps without a pinned value")
    args = ap.parse_args()
    deps = fetch(args.pin)
    print(json.dumps([{k: d[k] for k in ("name", "sha256", "bytes", "sha256_source")} for d in deps], indent=1))
    for d in deps:
        print(d["name"], "notices:", ", ".join(d["notices_in_package"][:12]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
