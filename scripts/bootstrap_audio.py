#!/usr/bin/env python3
"""Fetch the pinned third-party inputs of the native music renderer into .cache/audio.

Everything is downloaded from the projects' official GitHub repositories at a
fixed commit and verified by SHA-256. Nothing is installed system-wide.

  python3 scripts/bootstrap_audio.py            # fetch / verify
  python3 scripts/bootstrap_audio.py --verify   # verify only (no network)

Outputs (git-ignored cache):
  .cache/audio/TinySoundFont/{tsf.h,LICENSE}       MIT, SoundFont synthesizer
  .cache/audio/dr_libs/dr_mp3.h                    public domain / MIT-0, MP3 decoder
  .cache/audio/GeneralUser-GS/{GeneralUser-GS.sf2,LICENSE.txt}   GeneralUser GS License v2.0
"""
import argparse
import hashlib
import json
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / ".cache/audio"

PINS = [
    {
        "name": "TinySoundFont",
        "repo": "schellingb/TinySoundFont",
        "commit": "a3621a19c6504885cd9583e6e26c35bc954fc8ec",
        "files": {
            "tsf.h": "70d55963c98f60ebb81518eaa1f25d46888d5180eb5f5289fd6b74ffc177d197",
            "LICENSE": "ef10cb839de73fb6d5d32ea7151979e87809a6df70dc7e7ee8457ae1c9e7aa7e",
        },
    },
    {
        "name": "dr_libs",
        "repo": "mackron/dr_libs",
        "commit": "aec2dd20667f54acfdb66031217df6d240f56c52",  # "dr_mp3: Version 0.7.4"
        "files": {
            "dr_mp3.h": "f5c8020eeebb79877184432efdd2651f24869f5bb8f379d45450fd0aed1ee915",
        },
    },
    {
        "name": "GeneralUser-GS",
        "repo": "mrbumpy409/GeneralUser-GS",
        "commit": "684543d5e5efaef08d02be50dcda8d552478fa60",  # v2.0.3 (2026-02-22)
        "files": {
            "GeneralUser-GS.sf2": "9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe",
            "documentation/LICENSE.txt": "7b32efefdf95ce38a043799f0659853ddc00fbaa14d8c50f0aca16b9b8b405be",
        },
    },
]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--verify", action="store_true", help="only verify existing files")
    args = parser.parse_args()
    manifest = []
    failed = False
    for pin in PINS:
        for rel, digest in pin["files"].items():
            dest = CACHE / pin["name"] / Path(rel).name
            if not dest.exists() or sha256(dest) != digest:
                if args.verify:
                    print(f"MISSING/BAD {dest}")
                    failed = True
                    continue
                url = f"https://raw.githubusercontent.com/{pin['repo']}/{pin['commit']}/{rel}"
                dest.parent.mkdir(parents=True, exist_ok=True)
                tmp = dest.with_suffix(dest.suffix + ".part")
                print(f"fetch {url}")
                with urllib.request.urlopen(url, timeout=120) as response, open(tmp, "wb") as out:
                    while True:
                        block = response.read(1 << 20)
                        if not block:
                            break
                        out.write(block)
                if sha256(tmp) != digest:
                    tmp.unlink()
                    sys.exit(f"SHA-256 mismatch for {url}")
                tmp.replace(dest)
            manifest.append({"name": pin["name"], "repo": pin["repo"], "commit": pin["commit"], "path": rel,
                             "file": str(dest.relative_to(ROOT)), "sha256": digest})
    if failed:
        sys.exit(1)
    (CACHE / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"ok: {len(manifest)} files in {CACHE.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
