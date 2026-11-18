#!/usr/bin/env python3
"""Start the local native development build with a persistent private profile.

No build/download is performed. Run with --dry-run to review the command.
Original game assets stay read-only; each launch gets separate logs/screenshots.
"""
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


LEGACY_MANAGED_AX = Path("work/localization/managed-gmes-v1-01/start.ax")
LEGACY_MANAGED_SHA256 = "8aa19643c4824fa98ee175194864ecdfde8d4871c13a0d9f8ffa99cf9b99ad03"

MANAGED_AX = Path("work/localization/managed-home-v2/start.ax")
MANAGED_SHA256 = "bfb23aebb6a9a78983f717a361ce5691da4e520590d3013d686798e6e6d691b8"


def game_ax(root, language):
    original = root / "work/normalized/2.335/start.ax"
    if language == "ko":
        managed = root / MANAGED_AX
        if managed.is_file():
            if hashlib.sha256(managed.read_bytes()).hexdigest() != MANAGED_SHA256:
                raise RuntimeError(f"Unexpected Korean 2.335 AX hash: {managed}")
            return managed
        legacy = root / LEGACY_MANAGED_AX
        if legacy.is_file():
            if hashlib.sha256(legacy.read_bytes()).hexdigest() != LEGACY_MANAGED_SHA256:
                raise RuntimeError(f"Unexpected Korean 2.335 AX hash: {legacy}")
            print(f"Korean HUD/log AX missing at {managed}. Using paragraph AX {legacy}.", file=sys.stderr)
            return legacy
        print(f"Korean paragraph AX missing at {managed}. Using the original AX with menu translations.", file=sys.stderr)
    return original


@contextlib.contextmanager
def profile_lock(profile):
    # Keep the open descriptor locked until the child exits, including on error.
    # OS releases it after crashes; no stale PID lockfiles or game PID killing.
    profile.mkdir(parents=True, exist_ok=True)
    with (profile / ".native-player.lock").open("a+b") as lock:
        if os.name == "nt":
            import msvcrt
            lock.seek(0)
            lock.write(b"\0")
            lock.flush()
            lock.seek(0)
            try:
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as e:
                raise RuntimeError("This profile is already open in another launcher.") from e
        else:
            import fcntl
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as e:
                raise RuntimeError("This profile is already open in another launcher.") from e
        try:
            yield
        finally:
            if os.name == "nt":
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(lock, fcntl.LOCK_UN)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, help="override the native executable")
    parser.add_argument("--profile", type=Path, default=ROOT / "work/player/2.335/user")
    parser.add_argument("--language", choices=("original", "ko"), default="original",
                        help="ko enables the partial Korean display catalog; save data stays original")
    parser.add_argument("--diagnose-errors", action="store_true",
                        help="stop on the first VM error before the game's error handler")
    parser.add_argument("--audit-translation", action="store_true",
                        help="record untranslated displayed text privately in this session")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if sys.platform == "darwin":
        default = ROOT / "build/native-macos-arm64-legacy-cp932-release/elona_gui_host"
    elif os.name == "nt":
        default = ROOT / "build/native-windows-x64-legacy-cp932-debug/elona_gui_host.exe"
    else:
        parser.error("This project currently targets macOS ARM64 and Windows x64.")
    host = (args.host or default).resolve()
    profile = args.profile.resolve()
    original = (ROOT / "elonaplus2.335").resolve()
    ax = game_ax(ROOT, args.language)
    packed = ROOT / "work/extracted/2.335"
    if profile == original or original in profile.parents:
        parser.error("The writable profile cannot be inside the original game folder.")
    for path in (host, ax, packed / "scene1.hsp", packed / "scene2.hsp", original / "elonaplus.exe"):
        if not path.is_file():
            parser.error(f"Missing {path}; follow README build/preparation steps first.")
    base = [str(host), "--game", str(ax), "--game-root", str(original),
            "--user-root", str(profile), "--packed-dir", str(packed), "--auxiliary-close-hide"]
    if args.language == "ko":
        catalog = ROOT / "localization/ko-KR/runtime.tsv"
        if not catalog.is_file():
            parser.error(f"Missing Korean catalog: {catalog}")
        base += ["--translation", str(catalog)]
    if args.diagnose_errors:
        base += ["--fail-on-hsp-error"]
    if args.audit_translation and args.language != "ko":
        parser.error("--audit-translation requires --language ko")
    if args.dry_run:
        print(json.dumps({"command": base, "profile": str(profile),
                          "session_logs": str(ROOT / "work/player/2.335/sessions")}, indent=2))
        return 0
    with profile_lock(profile):
        sessions = ROOT / "work/player/2.335/sessions"
        sessions.mkdir(parents=True, exist_ok=True)
        session = Path(tempfile.mkdtemp(prefix="play-", dir=sessions))
        work = session / "cwd"
        work.mkdir()
        command = base + ["--work-dir", str(work), "--report", str(session / "report.json"),
                          "--screenshot", str(session / "screen0.bmp"), "--trace-commands"]
        if args.audit_translation:
            command += ["--translation-misses", str(session / "untranslated.tsv")]
        (session / "launch.json").write_text(json.dumps(command, indent=2) + "\n")
        print(f"Save/config folder: {profile}\nSession logs: {session}", flush=True)
        with (session / "stdout.log").open("wb") as out, (session / "stderr.log").open("wb") as err:
            code = subprocess.call(command, cwd=ROOT, stdout=out, stderr=err)
        if code:
            print(f"Game exited with code {code}; see {session / 'report.json'}", file=sys.stderr)
        return code


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        raise SystemExit(str(error))
