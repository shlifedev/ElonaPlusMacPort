#!/usr/bin/env python3
"""Check caught HSP errors, strict diagnostics and onexit without game assets.

Every run uses a new private directory. Errors and close events are injected
through actual HSP VM execution and the same host event path as SDL input.
"""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/graphics/fixtures"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--compiler", type=Path, required=True)
    ap.add_argument("--host", type=Path, required=True)
    ap.add_argument("--common", type=Path, required=True)
    ap.add_argument("--work", type=Path, required=True)
    ap.add_argument("--unique", action="store_true")
    args = ap.parse_args()
    work = args.work.resolve()
    if (ROOT / "elonaplus2.335").resolve() in [work, *work.parents]:
        ap.error("refusing original game directory")
    if work.exists() and not args.unique:
        ap.error(f"refusing to overwrite existing evidence: {work}")
    work.mkdir(parents=True, exist_ok=args.unique)
    if args.unique:
        work = Path(tempfile.mkdtemp(prefix="run-", dir=work))
    compiled = {}
    for name in ("caught_errors", "recovered_error", "close_handled", "close_cancelled", "fullscreen"):
        source, ax = work / f"{name}.hsp", work / f"{name}.ax"
        source.write_bytes((FIXTURES / f"{name}.hsp").read_text().encode("cp932"))
        cmd = [str(args.compiler.resolve()), "-d", f"--compath={args.common.resolve()}/", f"-o{ax}", str(source)]
        run = subprocess.run(cmd, capture_output=True, timeout=60)
        (work / f"{name}-compile.log").write_bytes(run.stdout + run.stderr)
        if run.returncode:
            raise SystemExit(f"compile failed: {source}")
        compiled[name] = ax
    close_script = work / "close.script"
    close_script.write_text("5 close\n")
    cases = [
        ("caught_errors", False, 0, 3, 0),
        ("caught_errors", True, 1, 1, 0),
        ("recovered_error", False, 7, 1, 0),
        ("recovered_error", True, 1, 1, 0),
        ("close_handled", False, 23, 0, 1),
        ("close_cancelled", False, 7, 0, 1),
        ("fullscreen", False, 0, 0, 0),
    ]
    results = []
    for name, strict, expected_exit, errors, closes in cases:
        case = work / f"{name}-{'strict' if strict else 'normal'}"
        case.mkdir()
        report = case / "report.json"
        cmd = [str(args.host.resolve()), "--trusted-fixture", str(compiled[name]), "--work-dir", str(case),
               "--deterministic", "--headless", "--max-waits", "100", "--timeout-ms", "10000",
               "--report", str(report), "--trace-commands"]
        if strict:
            cmd.append("--fail-on-hsp-error")
        if closes:
            cmd += ["--script", str(close_script)]
        run = subprocess.run(cmd, capture_output=True, timeout=20)
        (case / "host.log").write_bytes(run.stdout + run.stderr)
        rep = json.loads(report.read_text()) if report.exists() else {}
        observed = rep.get("observed_hsp_errors", [])
        passed = (run.returncode == expected_exit and rep.get("exit_code") == expected_exit
                  and rep.get("observed_hsp_error_count") == errors and len(observed) == errors
                  and all(e["code"] == 19 for e in observed)
                  and rep.get("close_requests") == closes and not rep.get("first_unsupported"))
        if strict:
            passed &= rep.get("exit_reason") == "hsp-error" and rep.get("end_pc_words") == -1
            passed &= "HSP error 19" in rep.get("hsp_error", "")
        else:
            passed &= rep.get("exit_reason") == "end" and rep.get("end_pc_words", -1) >= 0
            passed &= rep.get("hsp_error") == ""
        results.append({"case": case.name, "passed": bool(passed), "command": cmd, "report": rep})
        print(f"{'PASS' if passed else 'FAIL'} {case.name}")
    (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
