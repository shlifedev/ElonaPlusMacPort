#!/usr/bin/env python3
"""Run synthetic IME HSP assertions in a fresh private directory.

Does not drive the OS keyboard/IME. --window uses a hidden SDL window and
checks selection/rejection overlays read back from that renderer.
"""
import argparse
import importlib.util
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--compiler", type=Path, required=True)
    ap.add_argument("--host", type=Path, required=True)
    ap.add_argument("--common", type=Path, required=True)
    ap.add_argument("--work", type=Path, required=True)
    ap.add_argument("--window", action="store_true")
    ap.add_argument("--unique", action="store_true", help="create a fresh run-* subdirectory under --work (CTest)")
    args = ap.parse_args()
    work = args.work.resolve()
    if work.exists() and not args.unique:
        ap.error(f"refusing to overwrite existing evidence: {work}")
    if (ROOT / "elonaplus2.335").resolve() in [work, *work.parents]:
        ap.error("refusing original game directory")
    if args.unique:
        work.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="run-", dir=work))
    else:
        work.mkdir(parents=True)
    src = ROOT / "tests/graphics/fixtures"
    hsp = work / "ime_edit.hsp"
    hsp.write_bytes((src / "ime_edit.hsp").read_text().encode("cp932"))
    script = (src / "ime_edit.script").read_text()
    if not args.window:
        script = "\n".join(line for line in script.splitlines() if " capture " not in line)
    (work / "input.script").write_text(script)
    comp = subprocess.run([str(args.compiler.resolve()), "-d", f"--compath={args.common.resolve()}/",
                           f"-o{work / 'ime_edit.ax'}", str(hsp)], capture_output=True, timeout=60)
    (work / "compile.log").write_bytes(comp.stdout + comp.stderr)
    if comp.returncode:
        raise SystemExit(f"compile failed: {work / 'compile.log'}")
    cmd = [str(args.host.resolve()), "--trusted-fixture", str(work / "ime_edit.ax"), "--work-dir", str(work),
           "--deterministic", "--script", str(work / "input.script"), "--timeout-ms", "20000",
           "--report", str(work / "report.json"), "--screenshot", str(work / "screen0.bmp")]
    cmd += ["--hidden-window"] if args.window else ["--headless"]
    run = subprocess.run(cmd, capture_output=True, timeout=30)
    (work / "host.log").write_bytes(run.stdout + run.stderr)
    rep = json.loads((work / "report.json").read_text())
    notes = rep.get("notes", [])
    result = {"exit": run.returncode, "report": rep, "synthetic_events": True, "actual_os_ime_tested": False}
    ok = run.returncode == 0 and not rep["first_unsupported"] and not rep["hsp_error"]
    ok &= sum("Text limit reached" in n for n in notes) == 2
    ok &= sum("Cannot enter this character" in n for n in notes) == 1
    if args.window:
        spec = importlib.util.spec_from_file_location("gui_fixture", ROOT / "tests/graphics/run_gui_fixtures.py")
        mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
        _, _, preedit, _ = mod.decode_bmp(work / "composition.bmp")
        _, _, rejected, _ = mod.decode_bmp(work / "rejected.bmp")
        selected = sum(c == (0, 90, 190) for c in preedit.values())
        error = sum(c == (176, 0, 0) for c in rejected.values())
        result.update(selected_preedit_pixels=selected, rejected_input_red_pixels=error)
        ok &= selected > 10 and error > 10
    result["passed"] = bool(ok)
    (work / "results.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(f"{'PASS' if ok else 'FAIL'} synthetic IME composition/commit/cancel/CP932/editing ({'hidden SDL' if args.window else 'headless'})")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
