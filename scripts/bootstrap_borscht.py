#!/usr/bin/env python3
"""Build a pinned, locally patched borscht without changing game inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM_URL = "https://github.com/Ruin0x11/borscht.git"
UPSTREAM_COMMIT = "03c8be3398a59d9df445fd5a0c7850b3f2188869"
CACHE = ROOT / ".cache" / "upstream"
SOURCE = CACHE / "borscht"
PATCH = ROOT / "scripts" / "patches" / "borscht" / "0001-local-compatibility.patch"
MANIFEST = CACHE / "borscht-build.json"
BUILD_LOG = CACHE / "borscht-build.log"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture(args: list[str], cwd: Path | None = None) -> str:
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def bootstrap(cargo: str = "cargo") -> Path:
    CACHE.mkdir(parents=True, exist_ok=True)
    if not SOURCE.exists():
        subprocess.run(["git", "clone", "--config", "core.autocrlf=false", "--no-checkout", UPSTREAM_URL, str(SOURCE)], check=True)
        subprocess.run(["git", "checkout", "--detach", UPSTREAM_COMMIT], cwd=SOURCE, check=True)
    if not (SOURCE / ".git").exists():
        raise RuntimeError(f"Refusing to modify an existing non-git directory: {SOURCE}")
    actual_commit = capture(["git", "rev-parse", "HEAD"], SOURCE)
    if actual_commit != UPSTREAM_COMMIT:
        raise RuntimeError(f"Cached checkout is {actual_commit}, expected {UPSTREAM_COMMIT}; refusing to reset it")
    upstream_remote = capture(["git", "remote", "get-url", "origin"], SOURCE)
    if upstream_remote != UPSTREAM_URL:
        raise RuntimeError(f"Cached checkout has an unexpected origin: {upstream_remote}")
    patch = PATCH.read_bytes()
    actual_diff = subprocess.check_output(["git", "diff", "--binary", "HEAD", "--"], cwd=SOURCE)
    if not actual_diff:
        subprocess.run(["git", "apply", "--check", str(PATCH)], cwd=SOURCE, check=True)
        subprocess.run(["git", "apply", str(PATCH)], cwd=SOURCE, check=True)
    elif actual_diff != patch:
        raise RuntimeError("Cached borscht has changes outside the exact checked-in patch; refusing to overwrite them")
    untracked = capture(["git", "ls-files", "--others", "--exclude-standard"], SOURCE)
    if untracked:
        raise RuntimeError(f"Cached borscht has unexpected untracked source files: {untracked}")
    lock_before = sha256(SOURCE / "Cargo.lock")
    env = os.environ.copy()
    env["CARGO_TARGET_DIR"] = str(SOURCE / "target")
    command = [cargo, "build", "--locked", "--release", "-p", "borscht"]
    with BUILD_LOG.open("wb") as log:
        result = subprocess.run(command, cwd=SOURCE, env=env, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        tail = BUILD_LOG.read_text(errors="replace").splitlines()[-35:]
        raise RuntimeError(f"borscht build failed ({result.returncode}); see {BUILD_LOG}\n" + "\n".join(tail))
    lock_after = sha256(SOURCE / "Cargo.lock")
    if lock_before != lock_after:
        raise RuntimeError("Cargo.lock changed despite --locked")
    if subprocess.check_output(["git", "diff", "--binary", "HEAD", "--"], cwd=SOURCE) != patch:
        raise RuntimeError("The checkout changed unexpectedly while building")
    binary = SOURCE / "target" / "release" / ("borscht.exe" if os.name == "nt" else "borscht")
    record = {
        "upstream_url": UPSTREAM_URL,
        "upstream_commit": UPSTREAM_COMMIT,
        "patch_sha256": sha256(PATCH),
        "cargo_lock_sha256": lock_after,
        "dictionary_sha256": sha256(SOURCE / "Dictionary.csv"),
        "cargo_version": capture([cargo, "--version"]),
        "rustc_version": capture(["rustc", "--version"]),
        "command": command,
        "cwd": str(SOURCE),
        "binary": str(binary),
        "binary_sha256": sha256(binary),
        "build_log": str(BUILD_LOG),
    }
    MANIFEST.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return binary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cargo", default="cargo", help="Cargo executable; existing Rust toolchain is used and recorded")
    args = parser.parse_args()
    try:
        binary = bootstrap(args.cargo)
    except (OSError, subprocess.CalledProcessError, RuntimeError) as exc:
        print(f"bootstrap failed: {exc}", file=sys.stderr)
        return 1
    print(json.dumps({"binary": str(binary), "manifest": str(MANIFEST)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
