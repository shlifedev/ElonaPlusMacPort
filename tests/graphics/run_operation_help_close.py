#!/usr/bin/env python3
"""Check opt-in operation-help dismissal without changing main-window exit."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--compiler", type=Path, required=True)
    ap.add_argument("--host", type=Path, required=True)
    ap.add_argument("--common", type=Path, required=True)
    ap.add_argument("--work", type=Path, required=True)
    a = ap.parse_args()
    work = a.work.resolve()
    if work.exists():
        ap.error("work directory exists; choose a new path")
    if ROOT / "elonaplus2.335" in [work, *work.parents] or ROOT / "work/player" in [work, *work.parents]:
        ap.error("work must not be inside original game or player data")
    work.mkdir(parents=True)
    ax = work / "fixture.ax"
    src = ROOT / "tests/graphics/fixtures/operation_help_close.hsp"
    compiled = subprocess.run([str(a.compiler.resolve()), "-d", f"--compath={a.common.resolve()}/", f"-o{ax}", str(src)], capture_output=True)
    (work / "compile.log").write_bytes(compiled.stdout + compiled.stderr)
    if compiled.returncode:
        return compiled.returncode
    results = []
    for name, flag, actions, frame, closes, hidden in [
        ("reference_help_close", False, "5 close 20\n10 close 0\n", 5, 1, 0),
        ("native_help_then_main", True, "5 close 20\n10 close 0\n", 10, 2, 1),
        ("native_main_direct", True, "5 close 0\n", 5, 1, 0),
    ]:
        case = work / name
        case.mkdir()
        script = case / "input.script"
        script.write_text(actions)
        cmd = [str(a.host.resolve()), "--trusted-fixture", str(ax), "--work-dir", str(case), "--deterministic", "--headless",
               "--script", str(script), "--report", str(case / "report.json"), "--trace-commands", "--max-waits", "30"]
        if flag:
            cmd += ["--auxiliary-close-hide"]
        r = subprocess.run(cmd, capture_output=True, timeout=30)
        (case / "stderr.log").write_bytes(r.stderr)
        report = json.loads((case / "report.json").read_text())
        checks = {
            "original_onexit_return": r.returncode == report["exit_code"] == 23,
            "exit_frame": report["frames"] == frame,
            "close_count": report["close_requests"] == closes,
            "hidden_help_count": report["hidden_help_close_requests"] == hidden,
            "no_errors": not report["hsp_error"] and not report["first_unsupported"] and report["observed_hsp_error_count"] == 0,
        }
        results.append({"case": name, "checks": checks, "report": report, "command": cmd})
    reference = work / "reference_help_close"
    native = work / "native_help_then_main"
    display_checks = {
        "vm_text_unchanged": (reference / "vm-original.txt").read_bytes() == (native / "vm-original.txt").read_bytes(),
        "main_window_text_unchanged": (reference / "main.bmp").read_bytes() == (native / "main.bmp").read_bytes(),
        "help_notice_updated": (reference / "help.bmp").read_bytes() != (native / "help.bmp").read_bytes(),
    }
    result = {"passed": all(all(c["checks"].values()) for c in results) and all(display_checks.values()),
              "cases": results, "display_checks": display_checks}
    (work / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed": result["passed"], "cases": [{"case": x["case"], "checks": x["checks"]} for x in results], "display_checks": display_checks}, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
