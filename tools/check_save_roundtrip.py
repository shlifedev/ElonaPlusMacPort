#!/usr/bin/env python3
"""Copy a test profile, restore/inspect/resave through the original AX, compare.

Only creates a new work/game-run/<name> directory. No source profile, original
game file, or existing run directory is modified. Requires the existing native
host and normalized AX. Actual keyboard input is ignored (headless/deterministic).
"""
import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path

from inspect_save_state import compare, digest, manifest

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--profile", required=True, type=Path)
    ap.add_argument("--run", required=True)
    ap.add_argument("--slot", default="sav_Ari")
    ap.add_argument("--restart-same-profile", action="store_true",
                    help="after saving, launch a second process against the same private user directory")
    ap.add_argument("--host", type=Path, default=ROOT / "build/native-macos-arm64-legacy-cp932-release/elona_gui_host")
    args = ap.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", args.run):
        raise SystemExit("invalid run name")
    if not re.fullmatch(r"sav_[A-Za-z0-9_-]+", args.slot):
        raise SystemExit("invalid test slot name")
    source, host = args.profile.resolve(), args.host.resolve()
    if not (source / "save" / args.slot).is_dir():
        raise SystemExit("source test save missing")
    run = ROOT / "work/game-run" / args.run
    run.mkdir()  # Fail if evidence already exists.
    before = manifest(source)
    original = {str(p.relative_to(ROOT)): digest(p.read_bytes()) for p in
                (ROOT / "elonaplus2.335.zip", ROOT / "elonaplus2.335/elonaplus.exe")}
    shutil.copytree(source, run / "user")
    (run / "cwd").mkdir()
    argv = [str(host), "--game", str(ROOT / "work/normalized/2.335/start.ax"),
            "--game-root", str(ROOT / "elonaplus2.335"), "--user-root", str(run / "user"),
            "--work-dir", str(run / "cwd"), "--packed-dir", str(ROOT / "work/extracted/2.335"),
            "--script", str(ROOT / "tests/game/load_inspect_resave.script"), "--headless", "--deterministic",
            "--max-waits", "1400", "--timeout-ms", "120000", "--trace-commands",
            "--report", str(run / "report.json"), "--screenshot", str(run / "screen0.bmp")]
    (run / "argv.json").write_text(json.dumps(argv, indent=2) + "\n")
    with (run / "stdout.log").open("wb") as out, (run / "stderr.log").open("wb") as err:
        process = subprocess.run(argv, stdout=out, stderr=err, timeout=150)
    report = json.loads((run / "report.json").read_text())
    result = compare(source / "save" / args.slot, run / "user/save" / args.slot)
    result.update(source_profile_unchanged=before == manifest(source),
                  original_unchanged=all(digest((ROOT / p).read_bytes()) == sha for p, sha in original.items()),
                  host_sha256=digest(host.read_bytes()), process_exit=process.returncode,
                  report={k: report.get(k) for k in ("exit_code", "exit_reason", "frames", "hsp_error", "first_unsupported")})
    result["passed"] = (result["invariants_passed"] and result["source_profile_unchanged"] and
                        result["original_unchanged"] and process.returncode == 0 and report["exit_reason"] == "end"
                        and not report["hsp_error"] and not report["first_unsupported"])
    if args.restart_same_profile and result["passed"]:
        # Preserve exactly what the first process wrote, then reopen the same
        # physical user root (not another copy) with a new process and cwd.
        shutil.copytree(run / "user", run / "before-restart")
        restart = run / "restart"
        (restart / "cwd").mkdir(parents=True)
        argv2 = list(argv)
        for key, value in (("--work-dir", restart / "cwd"), ("--report", restart / "report.json"),
                           ("--screenshot", restart / "screen0.bmp")):
            argv2[argv2.index(key) + 1] = str(value)
        (restart / "argv.json").write_text(json.dumps(argv2, indent=2) + "\n")
        with (restart / "stdout.log").open("wb") as out, (restart / "stderr.log").open("wb") as err:
            p2 = subprocess.run(argv2, stdout=out, stderr=err, timeout=150)
        r2 = json.loads((restart / "report.json").read_text())
        restarted = compare(run / "before-restart/save" / args.slot, run / "user/save" / args.slot)
        restarted.update(process_exit=p2.returncode,
                         same_user_root=argv[argv.index("--user-root") + 1] == argv2[argv2.index("--user-root") + 1],
                         report={k: r2.get(k) for k in ("exit_code", "exit_reason", "frames", "hsp_error", "first_unsupported")})
        restarted["passed"] = (restarted["invariants_passed"] and restarted["same_user_root"] and
                               p2.returncode == 0 and r2["exit_reason"] == "end" and
                               not r2["hsp_error"] and not r2["first_unsupported"])
        (restart / "state-compare.json").write_text(json.dumps(restarted, ensure_ascii=False, indent=2) + "\n")
        result["same_profile_restart"] = {k: restarted[k] for k in ("passed", "same_user_root", "turn_counter_delta", "report")}
        result["passed"] = result["passed"] and restarted["passed"]
    (run / "state-compare.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({k: result[k] for k in ("passed", "invariants", "turn_counter_delta", "report")}, indent=2))
    return int(not result["passed"])


if __name__ == "__main__":
    raise SystemExit(main())
