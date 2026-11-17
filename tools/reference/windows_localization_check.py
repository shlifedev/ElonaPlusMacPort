#!/usr/bin/env python3
"""Extend a completed private gui-native run with Korean/close/modal checks."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from platform_remote import ROOT, REMOTE_BASE, check_run_name, ps, put_files, sftp
from windows_gui_verify import read_bmp, write_png, compare_scaled, read_png


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--run", required=True)
    ap.add_argument("--catalog", type=Path, default=ROOT / "localization/ko-KR/runtime.tsv")
    args = ap.parse_args()
    run = check_run_name(args.run)
    if not run.startswith("gui-native-"):
        ap.error("requires completed private gui-native run")
    stage = ROOT / "work/orchestration/windows-gui" / run / "localization-check"
    if stage.exists():
        ap.error("localization stage exists; choose another native run")
    ps(f"$d=Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\reference-build\\{run}'; if(Test-Path (Join-Path $d 'localization-check')){{throw 'evidence exists'}}; if(-not(Test-Path (Join-Path $d 'WindowsGuiProbe.exe'))){{throw 'native build not complete'}}; 'ready'")
    stage.mkdir(parents=True)
    display = (ROOT / "tests/localization/display.hsp").read_text()
    (stage / "ko-display.hsp").write_bytes(display.replace("\nend\n", '\nnotesave "capture-ready.txt"\nstop\nend\n').encode("cp932"))
    (stage / "ko-display.script").write_text("80 exit 0\n")
    (stage / "ko-modal.hsp").write_bytes((ROOT / "tests/graphics/fixtures/modal_contract.hsp").read_bytes())
    (stage / "ko-modal.script").write_bytes((ROOT / "tests/graphics/fixtures/modal_contract.script").read_bytes())
    (stage / "ko-help.hsp").write_bytes((ROOT / "tests/graphics/fixtures/operation_help_close.hsp").read_bytes())
    (stage / "ko-help.script").write_text("5 close 20\n10 close 0\n")
    (stage / "ko-runtime.tsv").write_bytes(args.catalog.read_bytes())
    files = {p.name: p for p in stage.iterdir()}
    files["windows_localization_check.ps1"] = ROOT / "tools/reference/windows_localization_check.ps1"
    files["ko-WindowsGuiProbe.cs"] = ROOT / "tools/reference/WindowsGuiProbe.cs"
    put_files(run, files)
    dest = ROOT / "work/windows/reference-build" / run
    try:
        out = ps(f"$p=Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\reference-build\\{run}\\input\\windows_localization_check.ps1'; & ([scriptblock]::Create([IO.File]::ReadAllText($p))) -RunName '{run}'", timeout=240)
        (stage / "remote-output.txt").write_text(out)
    finally:
        sftp(f'lcd "{dest}"\nget -R {REMOTE_BASE}/{run}/localization-check\n', timeout=300)
    d = dest / "localization-check"
    summary = json.loads((d / "summary.json").read_text(encoding="utf-8-sig"))
    checks = {}
    reports = {}
    for name in ("original", "korean", "window", "modal", "help-reference", "help-native"):
        rep = json.loads((d / name / "report.json").read_text())
        reports[name] = rep
        checks[name + "_no_errors"] = not rep["hsp_error"] and not rep["first_unsupported"] and rep["observed_hsp_error_count"] == 0
        checks[name + "_exit"] = rep["exit_code"] == (23 if name.startswith("help-") else 0)
    for name in ("original", "korean", "window"):
        checks[name + "_VM_unchanged"] = (d / name / "vm-value.txt").read_bytes() == b"Starting Menu"
    checks["translation_changed_pixels"] = (d / "original/screen.bmp").read_bytes() != (d / "korean/screen.bmp").read_bytes()
    checks["modal_results"] = [x["result"] for x in reports["modal"]["dialogs"]] == [1, 6, 7, 1]
    checks["help_reference_exit"] = reports["help-reference"]["frames"] == 5 and reports["help-reference"]["hidden_help_close_requests"] == 0
    checks["help_native_continues"] = reports["help-native"]["frames"] == 10 and reports["help-native"]["hidden_help_close_requests"] == 1
    checks["help_VM_unchanged"] = (d / "help-reference/vm-original.txt").read_bytes() == (d / "help-native/vm-original.txt").read_bytes()
    checks["help_main_pixels_unchanged"] = (d / "help-reference/main.bmp").read_bytes() == (d / "help-native/main.bmp").read_bytes()
    checks["help_warning_updated"] = (d / "help-reference/help.bmp").read_bytes() != (d / "help-native/help.bmp").read_bytes()
    capture = compare_scaled(read_bmp(d / "window/screen.bmp"), read_bmp(d / "window/window.bmp"), (8, 50, 250, 24))
    checks["native_renderer_capture"] = capture["ok"] and reports["window"]["display"] == "sdl3"
    probe = (d / "window-results/probe.log").read_text(encoding="utf-8-sig")
    checks["own_window_visible"] = bool(re.search(r"^korean-window\.visible_windows=[1-9]", probe, re.M))
    checks["session_clean"] = summary["session"]["probeCompleted"] and summary["session"]["taskRemoved"] and summary["session"]["leftoverAfterCleanup"] == 0
    checks["catalog_hash"] = summary["catalogSha256"] == hashlib.sha256((stage / "ko-runtime.tsv").read_bytes()).hexdigest()
    for name in ("original", "korean", "window"):
        write_png(read_bmp(d / name / "screen.bmp")[2], d / f"{name}.png")
    write_png(read_bmp(d / "window/window.bmp")[2], d / "window-with-edit.png")
    result = {"passed": all(checks.values()), "checks": checks, "renderer_capture": capture, "summary": summary}
    (d / "verify.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
