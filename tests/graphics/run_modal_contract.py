#!/usr/bin/env python3
"""Compile/run the dialog result contract in a new scratch directory.

Default mode uses scripted answers and does not prove native UI delivery.
--interactive uses real SDL dialogs: choose OK, Yes, No, OK in that order.
Only use an owned test window; the fixture never loads game/player data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--compiler", required=True, type=Path)
    ap.add_argument("--host", required=True, type=Path)
    ap.add_argument("--common", required=True, type=Path)
    ap.add_argument("--work", required=True, type=Path)
    ap.add_argument("--interactive", action="store_true")
    a = ap.parse_args()
    work = a.work.resolve()
    if work.exists():
        ap.error("work directory exists; choose a new path")
    if ROOT / "elonaplus2.335" in [work, *work.parents] or ROOT / "work/player" in [work, *work.parents]:
        ap.error("work must not be inside original game or player data")
    host, compiler, common = a.host.resolve(), a.compiler.resolve(), a.common.resolve()
    work.mkdir(parents=True)
    src = ROOT / "tests/graphics/fixtures/modal_contract.hsp"
    script = ROOT / "tests/graphics/fixtures/modal_contract.script"
    ax = work / "modal.ax"
    comp = subprocess.run([str(compiler), "-d", f"--compath={common}/", f"-o{ax}", str(src)],
                          cwd=work, capture_output=True, timeout=60)
    (work / "compile.log").write_bytes(comp.stdout + comp.stderr)
    if comp.returncode:
        return comp.returncode
    cmd = [str(host), "--trusted-fixture", str(ax), "--work-dir", str(work),
           "--report", str(work / "report.json"), "--trace-commands", "--timeout-ms", "60000"]
    if not a.interactive:
        cmd += ["--deterministic", "--headless", "--script", str(script)]
    result = subprocess.run(cmd, capture_output=True, timeout=300 if a.interactive else 60)
    (work / "stdout.log").write_bytes(result.stdout)
    (work / "stderr.log").write_bytes(result.stderr)
    rep = json.loads((work / "report.json").read_text())
    values = (work / "modal-results.txt").read_text().splitlines()
    expected = ["ok=1", "yes=6", "no=7", "warning_ok=1", "onexit_count=0"]
    checks = {
        "result_values": values == expected,
        "exit_zero": result.returncode == rep["exit_code"] == 0,
        "no_hsp_error": not rep["hsp_error"],
        "no_observed_error": rep.get("observed_hsp_error_count", -1) == 0,
        "no_unsupported": not rep["first_unsupported"],
        "no_close_request": rep.get("close_requests", -1) == 0,
    }
    # Newer hosts expose the popup record. Retain compatibility with the host
    # used for the first actual macOS button test; don't pretend it logged it.
    if "dialogs" in rep:
        checks["dialog_results"] = [d["result"] for d in rep["dialogs"]] == [1, 6, 7, 1]
        checks["dialog_complete"] = all(d["completed"] for d in rep["dialogs"])
    summary = {"passed": all(checks.values()), "interactive": a.interactive,
               "command": cmd, "host_sha256": hashlib.sha256(host.read_bytes()).hexdigest(),
               "checks": checks, "values": values}
    (work / "results.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
