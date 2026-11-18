#!/usr/bin/env python3
"""Compare fresh JP gameplay with/without managed Korean display hooks.

Requires previously compiled private reference and managed AX files. Uses new
profiles only; never imports or writes a player profile. Outputs may contain
private game/save data and must remain under work/.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from inspect_save_state import compare, inspect, manifest
from prepare_korean_game import check_output_path


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save_payloads(directory: Path) -> dict:
    result = {}
    for path in directory.rglob("*"):
        if not path.is_file():
            continue
        data = path.read_bytes()
        if data[:2] == b"\x1f\x8b":
            data = gzip.decompress(data)
        result[str(path.relative_to(directory))] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ("host", "reference-ax", "managed-ax", "catalog", "work"):
        ap.add_argument("--" + name, required=True, type=Path)
    args = ap.parse_args()
    destination = check_output_path(args.work)
    host, reference, managed, catalog = [p.resolve() for p in
                                        (args.host, args.reference_ax, args.managed_ax, args.catalog)]
    for path in (host, reference, managed, catalog):
        if not path.is_file():
            ap.error(f"input missing: {path}")
    destination.mkdir(parents=True)
    # Freeze the reviewed catalog while another agent may add more translations.
    private_catalog = destination / "catalog.tsv"
    shutil.copyfile(catalog, private_catalog)
    original = manifest(ROOT / "elonaplus2.335")
    zip_hash = sha(ROOT / "elonaplus2.335.zip")
    source = (ROOT / "tests/game/newgame_english.script").read_text()
    if source.count("120 key DOWN\n") != 1:
        raise ValueError("newgame script language-selection anchor changed")
    jp_script = destination / "newgame_japanese.script"
    jp_script.write_text(source.replace("120 key DOWN\n", ""))
    stages = [("newgame", jp_script, 4700),
              ("reload", ROOT / "tests/game/load_inspect_resave.script", 1400),
              ("world", Path(__file__).with_name("japanese_home_to_world.script"), 3200)]
    report = {"scope": "fresh JP native reference versus managed Korean display; deterministic headless",
              "host_sha256": sha(host), "reference_ax_sha256": sha(reference),
              "managed_ax_sha256": sha(managed), "catalog_sha256": sha(private_catalog),
              "runs": {}, "comparisons": {}, "passed": False}
    try:
        for variant, ax in (("reference", reference), ("korean", managed)):
            run_root = destination / variant
            user = run_root / "user"
            user.mkdir(parents=True)
            report["runs"][variant] = {}
            for stage, script, waits in stages:
                run = run_root / stage
                (run / "cwd").mkdir(parents=True)
                command = [str(host), "--game", str(ax), "--game-root", str(ROOT / "elonaplus2.335"),
                           "--user-root", str(user), "--work-dir", str(run / "cwd"),
                           "--packed-dir", str(ROOT / "work/extracted/2.335"),
                           "--headless", "--deterministic", "--max-waits", str(waits),
                           "--timeout-ms", "120000", "--trace-commands", "--script", str(script),
                           "--report", str(run / "report.json"), "--screenshot", str(run / "screen0.bmp")]
                if variant == "korean":
                    command += ["--translation", str(private_catalog)]
                (run / "argv.json").write_text(json.dumps(command, indent=2) + "\n")
                with (run / "stdout.log").open("wb") as stdout, (run / "stderr.log").open("wb") as stderr:
                    process = subprocess.run(command, stdout=stdout, stderr=stderr, cwd=ROOT, timeout=150)
                host_report = json.loads((run / "report.json").read_text())
                slot = user / "save/sav_Ari"
                state = inspect(slot) if slot.is_dir() else None
                calls = host_report["command_counts"].get("native_ko_gmes", 0)
                checks = {"exit0": process.returncode == host_report["exit_code"] == 0,
                          "reason_end": host_report["exit_reason"] == "end",
                          "no_hsp_error": not host_report["hsp_error"] and not host_report.get("observed_hsp_error_count", 0),
                          "no_unsupported": not host_report["first_unsupported"], "saved": state is not None}
                if stage == "world":
                    checks["north_tyris"] = state is not None and state["location"]["gdata20"] == 4
                    checks["coordinates"] = state is not None and (state["player"]["x"], state["player"]["y"]) == (22, 21)
                    if variant == "korean":
                        checks["native_paragraphs_reached"] = calls == 12
                    checks["norne_screenshots"] = all((run / "cwd/shots" / name).is_file() for name in
                                                       ("norne-home-1.bmp", "norne-home-2.bmp", "norne-world-4.bmp"))
                item = {"checks": checks, "state": state, "native_gmes_calls": calls,
                        "frames": host_report["frames"], "script_sha256": sha(script)}
                report["runs"][variant][stage] = item
                if slot.is_dir():
                    shutil.copytree(slot, run / "save_snapshot")
                print(variant, stage, checks, flush=True)
                if not all(checks.values()):
                    raise RuntimeError(f"{variant}/{stage} failed: {[k for k, v in checks.items() if not v]}")
        for stage, _, _ in stages:
            a = destination / "reference" / stage / "save_snapshot"
            b = destination / "korean" / stage / "save_snapshot"
            left, right = save_payloads(a), save_payloads(b)
            state_compare = compare(a, b)
            report["comparisons"][stage] = {
                "file_count_reference": len(left), "file_count_korean": len(right),
                "all_payloads_equal": left == right,
                "differences": sorted(k for k in left.keys() | right.keys() if left.get(k) != right.get(k)),
                "invariants": state_compare["invariants"], "turn_delta": state_compare["turn_counter_delta"],
                "display_changed": (destination / "reference" / stage / "screen0.bmp").read_bytes() !=
                                   (destination / "korean" / stage / "screen0.bmp").read_bytes(),
            }
        report["passed"] = all(r["all_payloads_equal"] for r in report["comparisons"].values())
    except Exception as exc:
        report["error"] = str(exc)
    finally:
        report["original_tree_unchanged"] = original == manifest(ROOT / "elonaplus2.335")
        report["original_zip_unchanged"] = zip_hash == sha(ROOT / "elonaplus2.335.zip")
        report["host_unchanged"] = report["host_sha256"] == sha(host)
        report["passed"] = report["passed"] and all(report[k] for k in
                                                    ("original_tree_unchanged", "original_zip_unchanged", "host_unchanged"))
        (destination / "summary.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        print("PASS" if report["passed"] else "FAIL", destination / "summary.json")
        if report.get("error"):
            print(report["error"], file=sys.stderr)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
