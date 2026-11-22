#!/usr/bin/env python3
"""Prepare the pinned universal SDL frameworks for the macOS ARM64 Release preset."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / ".cache/macos-gui"
DOWNLOADS = ROOT / ".cache/downloads/macos-gui"
# DMG and universal framework binary digests. SDL_ttf's 3.2.2 release
# predates GitHub's asset digests; its official download was hashed locally.
FRAMEWORKS = [
    ("SDL3", "SDL", "3.4.20", "36b7e4cefcf7d55d227b3df5e686f8f33ab34193173a976445d1b57c80009e44",
     "5decac42cfb7bf28fa815b1e59bdc62bc27eacc95128074f385fa959f1c3f08c"),
    ("SDL3_ttf", "SDL_ttf", "3.2.2", "2c7035eba9e63df137dbe95d2672f4c52e68d76bf6dd7c47a325f29bb6ba3cf7",
     "6ed34fe0fbcb750eb070696fe3ca24ce691d9f2a416aa4ad5eb78a1d1085306a"),
]


def verify_framework(bundle, name, digest):
    subprocess.run(["codesign", "--verify", "--strict", bundle], check=True)
    if hashlib.sha256((bundle / name).read_bytes()).hexdigest() != digest:
        raise ValueError(f"Unexpected framework binary in {bundle}")


def main():
    if sys.platform != "darwin":
        sys.exit("This bootstrap prepares macOS frameworks and requires macOS.")
    CACHE.mkdir(parents=True, exist_ok=True)
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    for name, repo, version, dmg_digest, binary_digest in FRAMEWORKS:
        release = f"{name}-{version}"
        destination = CACHE / release / f"{name}.framework"
        if destination.exists():
            verify_framework(destination, name, binary_digest)
            print(f"Verified {destination}")
            continue
        dmg = DOWNLOADS / f"{release}.dmg"
        with tempfile.TemporaryDirectory(dir=CACHE) as tmp:
            stage = Path(tmp)
            if not dmg.exists():
                url = f"https://github.com/libsdl-org/{repo}/releases/download/release-{version}/{dmg.name}"
                download = stage / dmg.name
                subprocess.run(["curl", "-fLsS", "--retry", "2", "-o", download, url], check=True)
                if hashlib.sha256(download.read_bytes()).hexdigest() != dmg_digest:
                    raise ValueError(f"Download checksum mismatch for {dmg.name}")
                download.replace(dmg)
            if hashlib.sha256(dmg.read_bytes()).hexdigest() != dmg_digest:
                raise ValueError(f"Cached DMG checksum mismatch for {dmg}")
            mount = stage / "volume"
            mount.mkdir()
            subprocess.run(["hdiutil", "attach", "-readonly", "-nobrowse", "-noautoopen",
                            "-mountpoint", mount, dmg], check=True)
            try:
                source = mount / f"{name}.xcframework/macos-arm64_x86_64/{name}.framework"
                bundle = stage / destination.name
                subprocess.run(["ditto", source, bundle], check=True)
            finally:
                subprocess.run(["hdiutil", "detach", mount], check=True)
            verify_framework(bundle, name, binary_digest)
            destination.parent.mkdir(exist_ok=True)
            bundle.rename(destination)
            print(f"Prepared {destination}")


if __name__ == "__main__":
    main()
