#!/usr/bin/env python3
"""Bounded 2.335 gameplay regression using new private profiles and copies.

No game logic/save edits. Requires prepared normalized AX and Release host.
This proves scripted headless paths, not physical input or whole-game support.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from inspect_save_state import compare, inspect, manifest


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--prefix", default="playability-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    ap.add_argument("--build", type=Path, default=ROOT / "build/native-macos-arm64-legacy-cp932-release")
    args = ap.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", args.prefix):
        ap.error("prefix must be a plain run name")
    build = args.build.resolve()
    host = build / "elona_gui_host"
    # (name, source stage, input, max waits, expected exit, save name, map, x, y)
    stages = [
        ("newgame", None, "newgame_english.script", 4700, 0, "Ari", 7, 12, 17),
        ("departure", "newgame", "home_departure.script", 1850, 124, "Ari", 7, 13, 21),
        ("world", "departure", "home_to_world.script", 2700, 0, "Ari", 4, 22, 21),
        ("vernis", "world", "world_to_vernis.script", 2900, 0, "Ari", 5, 26, 1),
        ("equipment", "vernis", "equipment_roundtrip.script", 1550, 0, "Ari", 5, 26, 1),
        ("combat", "world", "field_combat_loot.script", 2250, 0, "Ari", 2, 16, 11),
        ("reload", "combat", "reload_after_combat.script", 1300, 0, "Ari", 2, 16, 11),
        # Fairy speed/body/equipment differ: the same timed movement is not
        # expected to end on the warrior's square. Observed stable variant path.
        ("variant", None, "newgame_fairy_wizard.script", 4700, 0, "Mira", 7, 11, 13),
    ]
    evidence = ROOT / "work/playability" / args.prefix
    runs = ROOT / "work/game-run"
    if evidence.exists() or any((runs / f"{args.prefix}-{s[0]}").exists() for s in stages):
        ap.error("prefix already exists; choose a new one (no outputs will be overwritten)")
    if not host.is_file():
        ap.error(f"host missing: {host}")
    evidence.mkdir(parents=True)
    host_hash = sha(host)
    original = manifest(ROOT / "elonaplus2.335")
    zip_hash = sha(ROOT / "elonaplus2.335.zip")
    summary = {
        "mode": "deterministic headless; virtual input; null audio output",
        "host": str(host), "host_sha256": host_hash,
        "normalized_ax_sha256": sha(ROOT / "work/normalized/2.335/start.ax"),
        "original_tree_files": len(original), "original_zip_sha256": zip_hash,
        "stages": [], "passed": False,
    }
    try:
        for name, parent, script, waits, expected, character, area, x, y in stages:
            run_name = f"{args.prefix}-{name}"
            run = runs / run_name
            env = os.environ.copy()
            env["ELONA_BUILD"] = str(build)
            env.pop("REUSE_USER_FROM", None)
            before = None
            source = None
            if parent:
                env["REUSE_USER_FROM"] = f"{args.prefix}-{parent}"
                source = runs / env["REUSE_USER_FROM"] / "user"
                before = manifest(source)
            script_path = ROOT / "tests/game" / script
            cmd = [str(ROOT / "tools/run_game_trace.sh"), run_name,
                   "--deterministic", "--headless", "--max-waits", str(waits),
                   "--timeout-ms", "60000", "--script", str(script_path)]
            print(f"Running {run_name}", flush=True)
            with (evidence / f"{name}.log").open("w") as log:
                result = subprocess.run(cmd, cwd=ROOT, env=env, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=120)
            report = json.loads((run / "report.json").read_text())
            save = run / "user/save" / f"sav_{character}"
            state = inspect(save)
            screenshots = list((run / "cwd/shots").glob("*.bmp"))
            checks = {
                "exit_code": result.returncode == expected == report["exit_code"],
                "exit_reason": report["exit_reason"] == ("max-waits" if expected == 124 else "end"),
                "no_hsp_error": not report["hsp_error"],
                "no_unsupported": not report["first_unsupported"],
                "screenshots_written": bool(screenshots),
                "original_zip_exe_unchanged": (run / "original-before.sha256").read_bytes() == (run / "original-after.sha256").read_bytes(),
                "source_profile_unchanged": source is None or before == manifest(source),
                "host_unchanged": sha(host) == host_hash,
                "saved_location": state["location"]["gdata20"] == area,
                "saved_coordinates": (state["player"]["x"], state["player"]["y"]) == (x, y),
                "saved_name": state["name"] == character,
            }
            summary["stages"].append({
                "name": name, "command": cmd, "source_profile": str(source) if source else None,
                "script_sha256": sha(script_path), "report": str(run / "report.json"),
                "frames": report["frames"], "exit_code": result.returncode,
                "screenshots": [str(p) for p in sorted(screenshots)],
                "state": state, "checks": checks,
            })
            if not all(checks.values()):
                raise RuntimeError(f"{name} failed: {[k for k,v in checks.items() if not v]}")
            print(f"  pass: {state['header']}, frame {report['frames']}, exit {result.returncode}", flush=True)
        left = runs / f"{args.prefix}-combat/user/save/sav_Ari"
        right = runs / f"{args.prefix}-reload/user/save/sav_Ari"
        roundtrip = compare(left, right)
        (evidence / "combat-roundtrip.json").write_text(json.dumps(roundtrip, ensure_ascii=False, indent=2) + "\n")
        summary["reload_invariants"] = roundtrip["invariants"]
        if not roundtrip["invariants_passed"]:
            raise RuntimeError("combat save reload changed an invariant")
        summary["passed"] = True
    except Exception as exc:
        summary["error"] = str(exc)
    finally:
        summary["original_tree_unchanged"] = original == manifest(ROOT / "elonaplus2.335")
        summary["original_zip_unchanged"] = zip_hash == sha(ROOT / "elonaplus2.335.zip")
        summary["passed"] = summary["passed"] and summary["original_tree_unchanged"] and summary["original_zip_unchanged"]
        out = evidence / "summary.json"
        out.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n")
        print(f"{'PASS' if summary['passed'] else 'FAIL'}: {out}", flush=True)
        if summary.get("error"):
            print(summary["error"], file=sys.stderr)
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
