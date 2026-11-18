#!/usr/bin/env python3
"""Build current Windows host privately; check Korean paragraph ABI and game saves.

This is a headless SDL_ttf/rendering check, not another PrintWindow baseline.
No installed game, user profile, display mode, or interactive window is touched.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import sys
import zipfile

from platform_remote import ROOT, REMOTE_BASE, check_run_name, make_remote_dir, ps, put_files, sftp
from windows_gui_run import make_snapshot, add
from windows_gui_verify import read_bmp, write_png

sys.path.insert(0, str(ROOT / "tools"))
from inspect_save_state import inspect, compare


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def payload_manifest(path):
    out = {}
    for p in sorted(path.rglob("*")):
        if p.is_file():
            data = p.read_bytes()
            if data[:2] == b"\x1f\x8b":
                data = gzip.decompress(data)
            out[p.relative_to(path).as_posix()] = hashlib.sha256(data).hexdigest()
    return out


def unpack_evidence(archive, dest):
    # .NET Framework ZIP writers preserve Windows separators in member names.
    with zipfile.ZipFile(archive) as zf:
        for member in zf.infolist():
            rel = Path(member.filename.replace("\\", "/"))
            if rel.is_absolute() or ".." in rel.parts:
                raise ValueError("unsafe evidence archive member")
            path = dest / rel
            if member.is_dir() or member.filename.endswith(("/", "\\")):
                path.mkdir(parents=True, exist_ok=True)
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(zf.read(member))


def verify(dest, manifest):
    ev = dest / "evidence"
    stages = manifest.get("game_stages", ["newgame", "reload", "departure", "world"])
    summary = json.loads((ev / "summary.json").read_text(encoding="utf-8-sig"))
    checks = {"build": summary["buildExit"] == 0,
              "catalog_unit": summary.get("catalogTestExit") == 0,
              "zip_unchanged": summary.get("zipUnchanged", False),
              "private_original_unchanged": summary.get("privateUnchanged", False),
              "no_owned_processes": summary.get("leftoverProcesses") == 0,
              "catalog_hash": summary.get("catalogSha256") == manifest["catalog_sha256"]}
    reports = {}
    for item in summary.get("runs", []):
        name = item["name"]
        report_path = ev / name / "report.json"
        rep = json.loads(report_path.read_text()) if report_path.exists() else {}
        reports[name] = rep
        expected = 124 if name.endswith("/departure") else 0
        checks[name] = (item["exit"] == expected and not item["timedOut"] and rep.get("exit_code") == expected
                        and not rep.get("first_unsupported", "missing") and not rep.get("hsp_error", "missing")
                        and rep.get("observed_hsp_error_count", -1) == 0)
    for name in ("fixture-original", "fixture-korean"):
        checks[name + "_VM"] = (ev / name / "vm-value.txt").read_bytes() == b"Starting Menu"
    checks["display_changed"] = (ev / "fixture-original/screen.bmp").read_bytes() != (ev / "fixture-korean/screen.bmp").read_bytes()
    checks["paragraph_VM"] = (ev / "fixture-paragraph/user/user/vm-paragraph.txt").read_bytes() == b"<emp1>Read<def> a spellbook."
    checks["paragraph_ABI_executed"] = reports["fixture-paragraph"].get("command_counts", {}).get("native_ko_gmes") == 2
    misses = [s for s in (ev / "fixture-catalog/misses.tsv").read_text().splitlines() if s and not s.startswith("#")]
    checks["catalog_all_keys"] = (not misses and reports["fixture-catalog"].get("command_counts", {}).get("mes") == manifest["catalog_keys"])
    comparisons = {}
    for variant in ("managed-fallback", "managed-korean"):
        comparisons[variant] = {}
        for stage in stages:
            a, b = ev / "reference" / stage / "save_snapshot", ev / variant / stage / "save_snapshot"
            am, bm = payload_manifest(a), payload_manifest(b)
            item = {"files_reference": len(am), "files_variant": len(bm), "payloads_identical": am == bm,
                    "differences": sorted(k for k in am.keys() | bm.keys() if am.get(k) != bm.get(k)),
                    "state": inspect(b) if (b / "gdata.s1").exists() else None}
            if (a / "gdata.s1").exists() and (b / "gdata.s1").exists():
                c = compare(a, b)
                item.update(invariants=c["invariants"], turn_delta=c["turn_counter_delta"])
            checks[f"{variant}/{stage}_save_equal"] = len(am) >= 154 and am == bm
            if variant == "managed-fallback":
                item["screen_identical"] = (ev / "reference" / stage / "screen.bmp").read_bytes() == (ev / variant / stage / "screen.bmp").read_bytes()
                checks[f"{variant}/{stage}_screen_equal"] = item["screen_identical"]
            comparisons[variant][stage] = item
    checks["native_ABI_in_game"] = reports["managed-korean/world"].get("command_counts", {}).get("native_ko_gmes", 0) == 12
    if "game_stages" in manifest:
        for variant in ("reference", "managed-fallback", "managed-korean"):
            state = inspect(ev / variant / "world/save_snapshot")
            checks[variant + "_north_tyris"] = state["location"]["gdata20"] == 4 and (state["player"]["x"], state["player"]["y"]) == (22, 21)
            checks[variant + "_norne_pages"] = all((ev / variant / "world/cwd/shots" / name).is_file() for name in ("norne-home-1.bmp", "norne-home-2.bmp", "norne-world-4.bmp"))
    for name in ("fixture-original", "fixture-korean", "fixture-paragraph"):
        write_png(read_bmp(ev / name / "screen.bmp")[2], ev / name / "screen.png")
    for variant in ("reference", "managed-korean"):
        for p in (ev / variant / "world/cwd/shots").glob("*.bmp"):
            write_png(read_bmp(p)[2], p.with_suffix(".png"))
    result = {"passed": all(checks.values()), "checks": checks, "comparisons": comparisons,
              "summary": summary, "inputs": manifest, "catalog_misses": misses,
              "reports": {k: {n: v.get(n) for n in ("exit_code", "exit_reason", "frames", "first_unsupported", "hsp_error", "observed_hsp_error_count", "command_counts")} for k, v in reports.items()},
              "scope": "Windows x64 headless TTF/paragraph + JP-based deterministic game, not OS window presentation or fullscreen."}
    (dest / "verify.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--run", required=True)
    ap.add_argument("--catalog", type=Path, required=True)
    ap.add_argument("--managed-ax", type=Path, default=ROOT / "work/localization/managed-gmes-v1-01/start.ax")
    ap.add_argument("--reference-ax", type=Path, default=ROOT / "work/localization/cp932-full-reference-01/start.ax")
    ap.add_argument("--japanese-script", type=Path, default=ROOT / "work/localization/newgame_japanese.script")
    args = ap.parse_args()
    run = check_run_name(args.run)
    if not run.startswith("gui-native-"):
        ap.error("run must start gui-native-")
    stage = ROOT / "work/orchestration/windows-managed" / run
    stage.mkdir(parents=True, exist_ok=False)
    snap = stage / "snapshot"
    info = make_snapshot(snap)
    shutil.copytree(ROOT / "tests", snap / "tests", dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__"))
    (stage / "snapshot-info.json").write_text(json.dumps(info, indent=2) + "\n")
    with zipfile.ZipFile(stage / "snapshot.zip", "w") as zf:
        for p in sorted(snap.rglob("*")):
            if p.is_file():
                add(zf, p.read_bytes(), p.relative_to(snap).as_posix())
    catalog = args.catalog.read_bytes()
    (stage / "runtime.tsv").write_bytes(catalog)
    def unescape(s):
        out, i = "", 0
        while i < len(s):
            if s[i] == "\\":
                i += 1
                out += {"t": "\t", "\\": "\\"}[s[i]]
            else:
                out += s[i]
            i += 1
        return out
    rows = [unescape(s.split("\t")[0]) for s in catalog.decode("utf-8").splitlines() if s and not s.startswith("#")]
    source = ['screen 0,640,480', 'font "MS Gothic",14', 'color 0,0,0']
    for s in rows:
        source.append('pos 0,0 : mes "' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\t", "\\t") + '"')
    source += ["await 1", "end 0"]
    (stage / "all-catalog.hsp").write_bytes(("\r\n".join(source) + "\r\n").encode("cp932"))
    (stage / "display.tsv").write_text("Starting Menu\t모험의 이정표\n", encoding="utf-8")
    (stage / "paragraph.tsv").write_text("<emp1>Read<def> a spellbook.\t<emp1>마법서<def>를 읽으면 주문을 배울 수 있다.\n", encoding="utf-8")
    lock = json.loads((ROOT / "tools/reference/windows_gui_deps.lock.json").read_text())
    manifest = {"catalog_sha256": hashlib.sha256(catalog).hexdigest(), "catalog_keys": len(rows),
                "game_stages": ["newgame", "reload", "world"],
                "managed_ax_sha256": digest(args.managed_ax), "reference_ax_sha256": digest(args.reference_ax),
                "snapshot_sha256": digest(stage / "snapshot.zip"), "deps": [{"name": n, "sha256": v["sha256"]} for n, v in lock.items()]}
    (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    uploads = {n: stage / n for n in ("snapshot.zip", "manifest.json", "runtime.tsv", "all-catalog.hsp", "display.tsv", "paragraph.tsv")}
    uploads.update({"managed.ax": args.managed_ax, "reference.ax": args.reference_ax,
                    "newgame-jp.script": args.japanese_script,
                    "windows_managed_check.ps1": ROOT / "tools/reference/windows_managed_check.ps1"})
    for n in ("scene1.hsp", "scene2.hsp"):
        uploads[n] = ROOT / "work/extracted/2.335" / n
    for n in ("load_inspect_resave.script",):
        uploads[n] = ROOT / "tests/game" / n
    uploads["japanese_home_to_world.script"] = ROOT / "tests/localization/source_transform/japanese_home_to_world.script"
    for d in manifest["deps"]:
        uploads[d["name"]] = ROOT / ".cache/downloads/windows-gui" / d["name"]
    make_remote_dir(run)
    put_files(run, uploads)
    print("staged", run, manifest["catalog_keys"], "catalog keys", flush=True)
    dest = ROOT / "work/windows/reference-build" / run
    dest.mkdir(parents=True, exist_ok=False)
    try:
        out = ps(f"$p=Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\reference-build\\{run}\\input\\windows_managed_check.ps1'; & ([scriptblock]::Create([IO.File]::ReadAllText($p))) -RunName '{run}'", timeout=3600)
        (dest / "remote-stdout.txt").write_text(out)
    finally:
        sftp(f'lcd "{dest}"\n-get {REMOTE_BASE}/{run}/build.log\n-get {REMOTE_BASE}/{run}/evidence.zip\n', timeout=600)
    unpack_evidence(dest / "evidence.zip", dest / "evidence")
    result = verify(dest, manifest)
    print(json.dumps({k: result[k] for k in ("passed", "checks", "scope")}, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
