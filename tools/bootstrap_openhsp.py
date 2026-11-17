#!/usr/bin/env python3
"""Acquire a pinned sparse OpenHSP checkout; never install system packages."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CHECKOUT = ROOT / ".cache/upstream/OpenHSP"
COMMIT = "3dbb8729dad53ee876fc681696c8fe542698860b"
URL = "https://github.com/onitama/OpenHSP.git"
PATCH = ROOT / "patches/openhsp/0001-opt-in-legacy-compatibility.patch"


def run(*args: str, cwd: Path = ROOT) -> str:
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def main() -> None:
    CHECKOUT.parent.mkdir(parents=True, exist_ok=True)
    fresh = not CHECKOUT.exists()
    if fresh:
        run("git", "clone", "--filter=blob:none", "--no-checkout", URL, str(CHECKOUT))
    if run("git", "remote", "get-url", "origin", cwd=CHECKOUT) != URL:
        raise SystemExit("Refusing to alter a checkout with a different origin")
    patched = False
    if not fresh and run("git", "status", "--porcelain", cwd=CHECKOUT):
        # Only our exact, pinned patch is accepted. Never erase other edits.
        patched = (run("git", "rev-parse", "HEAD", cwd=CHECKOUT) == COMMIT
                   and not run("git", "ls-files", "--others", "--exclude-standard", cwd=CHECKOUT)
                   and run("git", "diff", "HEAD", "--binary", cwd=CHECKOUT) == PATCH.read_text().strip())
        if not patched:
            raise SystemExit("Refusing to alter a checkout with unrecognized modifications")
    if patched:
        print(f"OpenHSP {COMMIT} + verified legacy opt-in patch: {CHECKOUT}")
        return
    run("git", "sparse-checkout", "set", "src/hsp3", "src/hspcmp", "common", cwd=CHECKOUT)
    if run("git", "rev-parse", "HEAD", cwd=CHECKOUT) != COMMIT:
        run("git", "fetch", "origin", COMMIT, cwd=CHECKOUT)
    run("git", "checkout", "--detach", COMMIT, cwd=CHECKOUT)
    run("git", "apply", "--check", str(PATCH), cwd=CHECKOUT)
    run("git", "apply", str(PATCH), cwd=CHECKOUT)
    print(f"OpenHSP {COMMIT} + legacy opt-in patch: {CHECKOUT}")


if __name__ == "__main__":
    main()
