#!/usr/bin/env python3
"""Copy a Mac test profile to a fresh Windows run and restore/inspect/resave.

Requires a previously verified gui-native-* host. Never changes that host or
its profiles. Uses the authorized home/windows test workspace, headless only.
"""
import argparse
import json
import sys
import zipfile
from pathlib import Path

from platform_remote import ROOT, REMOTE_BASE, check_run_name, make_remote_dir, put_files, ps, sftp
sys.path.insert(0, str(ROOT / "scripts"))
from inspect_save_state import compare, manifest


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--profile", type=Path, required=True)
    ap.add_argument("--run", required=True)
    ap.add_argument("--reference-run", default="gui-native-02")
    args = ap.parse_args()
    run = check_run_name(args.run)
    ref = check_run_name(args.reference_run)
    if not run.startswith("platform-save-") or not ref.startswith("gui-native-"):
        raise SystemExit("invalid run prefix")
    profile = args.profile.resolve()
    if not (profile / "save/sav_Ari").is_dir():
        raise SystemExit("test profile has no sav_Ari")
    local = ROOT / "work/windows/reference-build" / run
    local.mkdir()
    before = manifest(profile)
    with zipfile.ZipFile(local / "profile.zip", "w", zipfile.ZIP_DEFLATED) as z:
        for p in sorted(profile.rglob("*")):
            if p.is_file():
                z.write(p, p.relative_to(profile).as_posix())
    make_remote_dir(run)
    put_files(run, {"profile.zip": local / "profile.zip",
                    "load_inspect_resave.script": ROOT / "tests/game/load_inspect_resave.script",
                    "save_roundtrip_windows.ps1": Path(__file__).with_suffix(".ps1")})
    try:
        output = ps(f"""
$s = [IO.File]::ReadAllText((Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\reference-build\\{run}\\input\\save_roundtrip_windows.ps1'))
& ([scriptblock]::Create($s)) -RunName '{run}' -ReferenceRun '{ref}'
""", timeout=300)
        (local / "remote-stdout.json").write_text(output)
    finally:
        sftp(f'lcd "{local}"\n-get -R {REMOTE_BASE}/{run}/result\n', timeout=300)
    result = compare(profile / "save/sav_Ari", local / "result/user/save/sav_Ari")
    ev = json.loads((local / "result/run-evidence.json").read_text())
    report = json.loads((local / "result/report.json").read_text())
    result.update(source_profile_unchanged=before == manifest(profile), run_evidence=ev,
                  report={k: report.get(k) for k in ("exit_code", "exit_reason", "frames", "hsp_error", "first_unsupported")})
    result["passed"] = (result["invariants_passed"] and result["source_profile_unchanged"] and ev["exit"] == 0
                        and not ev["timedOut"] and ev["originalZipUnchanged"] and ev["privateGameUnchanged"]
                        and ev["profileZipUnchanged"] and ev["ownedProcessExited"] and report["exit_reason"] == "end"
                        and not report["hsp_error"] and not report["first_unsupported"])
    (local / "state-compare.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({k: result[k] for k in ("passed", "invariants", "turn_counter_delta", "report")}, indent=2))
    return int(not result["passed"])


if __name__ == "__main__":
    raise SystemExit(main())
