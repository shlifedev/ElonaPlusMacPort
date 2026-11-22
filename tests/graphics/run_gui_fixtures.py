#!/usr/bin/env python3
"""Compile tests/gui fixtures (CP932) and run them in elona_gui_host.

Each fixture runs in its own scratch directory under --work (never the game
folder). Checks exit codes, decodes produced BMP files, and records a JSON
summary. Unsupported functionality must fail explicitly (exit 3 + diagnostic),
never pass silently.
"""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GUI = ROOT / "tests/gui"
LOCAL = Path(__file__).resolve().parent / "fixtures"

# name -> expectation
FIXTURES = {
    "pixel_roundtrip": {"exit": 0},
    "color_key": {"exit": 0},
    "bitmap_export": {"exit": 0, "bmp": ("fixture-buffer.bmp", 8, 8, lambda x, y: (17, 34, 51))},
    # mesbox/objprm/objsel (virtual edit control). IME composition is not
    # covered by this automatic check.
    "input_control": {"exit": 0, "overlay": (0, 0, 240, 26)},
}
# Windows HSP 3.4 x86 observation fixtures (scripts/reference/gui_fixtures, read only).
# Every .bmp/.txt the Windows run produced is compared byte-for-byte when
# --windows-reference is given; the pattern is also checked analytically.
REFERENCE = ROOT / "scripts/reference/gui_fixtures"
REFERENCE_FIXTURES = {
    "gui_pattern_dump": {"exit": 0, "bmp": ("pt-screen0.bmp", 48, 48, None)},
    "gui_colorkey_dump": {"exit": 0},
    "gui_pixel_dump": {"exit": 0},
}
LOCAL_FIXTURES = {
    "text_wait": {"exit": 0, "text": True},
    "unsupported_command": {"exit": 3, "unsupported": "mci"},
    "input_script": {"exit": 0},
    "edit_keylog": {"exit": 0},
    "width_map": {"exit": 0, "game": True},
}


def decode_bmp(path):
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError("not a BMP")
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp != 24 or h <= 0:
        raise ValueError(f"unexpected BMP layout bpp={bpp} h={h}")
    stride = (w * 3 + 3) & ~3
    px = {}
    for y in range(h):
        row = off + (h - 1 - y) * stride
        for x in range(w):
            b, g, r = data[row + x * 3: row + x * 3 + 3]
            px[(x, y)] = (r, g, b)
    return w, h, px, data


def pattern_expected(x, y):
    pat = lambda px, py: (px * 17 % 256, py * 17 % 256, (px + py) * 8 % 256)
    if y < 16 and x < 16:
        return pat(x, y)
    if y < 16 and 16 <= x < 32:
        p = pat(x - 16, y)
        return (255, 255, 255) if p == (0, 0, 0) else p
    if 16 <= y < 48 and x < 32:
        return pat(x // 2, (y - 16) // 2)
    return (255, 255, 255)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--compiler", type=Path, required=True)
    ap.add_argument("--host", type=Path, required=True)
    ap.add_argument("--common", type=Path, required=True)
    ap.add_argument("--work", type=Path, required=True)
    ap.add_argument("--headless", action="store_true", help="no window (default: real SDL window, deterministic input)")
    ap.add_argument("--hidden-window", action="store_true")
    ap.add_argument("--no-text", action="store_true", help="build without SDL3_ttf: text fixture must fail explicitly")
    ap.add_argument("--windows-reference", type=Path, help="dir with <fixture>/ outputs from the Windows x86 run (cases/)")
    args = ap.parse_args()
    work = args.work.resolve()
    if (ROOT / "elonaplus2.335").resolve() in [work, *work.parents]:
        sys.exit("refusing to run inside the original game folder")
    if work.exists():
        shutil.rmtree(work)
    src = work / "src"
    src.mkdir(parents=True)
    names = {**FIXTURES, **REFERENCE_FIXTURES, **LOCAL_FIXTURES}
    for name in names:
        base = GUI if name in FIXTURES else REFERENCE if name in REFERENCE_FIXTURES else LOCAL
        text = (base / f"{name}.hsp").read_bytes().decode("utf-8")
        (src / f"{name}.hsp").write_bytes(text.encode("cp932"))
    results = []
    ok = True
    for name, exp in names.items():
        exp = dict(exp)
        if exp.get("text") and args.no_text:
            exp = {"exit": 3, "unsupported": "mes"}
        hsp = src / f"{name}.hsp"
        ax = src / f"{name}.ax"
        comp = subprocess.run([str(args.compiler), "-d", f"--compath={args.common}/", f"-o{ax}", str(hsp)],
                              cwd=src, capture_output=True, timeout=60)
        row = {"fixture": name, "expected_exit": exp["exit"]}
        if comp.returncode:
            row.update(passed=False, stage="compile", log=(comp.stdout + comp.stderr).decode("utf-8", "replace"))
            results.append(row); ok = False; continue
        scratch = work / "run" / name
        scratch.mkdir(parents=True)
        report = scratch / "report.json"
        shot = work / "screens" / f"{name}-screen0.bmp"
        mode = ["--trusted-fixture", str(ax)]
        if exp.get("game"):
            game, user = scratch / "game", scratch / "user"
            game.mkdir()
            user.mkdir()
            mode = ["--game", str(ax), "--game-root", str(game), "--user-root", str(user)]
        cmd = [str(args.host), *mode, "--work-dir", str(scratch), "--deterministic",
               "--max-waits", "600", "--timeout-ms", "20000", "--report", str(report), "--screenshot", str(shot),
               "--trace-commands"]
        script = LOCAL / f"{name}.script"
        if script.exists():
            cmd += ["--script", str(script)]
        if args.headless:
            cmd.append("--headless")
        else:
            if args.hidden_window:
                cmd.append("--hidden-window")
            cmd += ["--window-capture", str(work / "screens" / f"{name}-window.bmp")]
        run = subprocess.run(cmd, capture_output=True, timeout=60)
        rep = json.loads(report.read_text()) if report.exists() else {}
        row.update(exit=run.returncode, stderr=run.stderr.decode("utf-8", "replace"), report=rep,
                   ax_sha256=hashlib.sha256(ax.read_bytes()).hexdigest(), screenshot=str(shot))
        passed = run.returncode == exp["exit"]
        if "unsupported" in exp:
            passed = passed and exp["unsupported"] in rep.get("first_unsupported", "")
        if "bmp" in exp and passed:
            fname, w, h, fn = exp["bmp"]
            bmp = scratch / fname
            if not bmp.exists():
                passed = False; row["bmp_error"] = "missing " + fname
            else:
                bw, bh, px, raw = decode_bmp(bmp)
                row["bmp"] = {"path": str(bmp), "width": bw, "height": bh, "bytes": len(raw),
                              "sha256": hashlib.sha256(raw).hexdigest()}
                fn = fn or pattern_expected
                bad = [(x, y, px[(x, y)], fn(x, y)) for y in range(bh) for x in range(bw) if px[(x, y)] != fn(x, y)]
                row["bmp"]["mismatches"] = len(bad)
                row["bmp"]["first_mismatches"] = [str(b) for b in bad[:5]]
                passed = passed and (bw, bh) == (w, h) and not bad
        if exp.get("text") and passed and shot.exists():
            _, _, px, _ = decode_bmp(shot)
            dark = sum(1 for v in px.values() if v == (0, 0, 0))
            colored = sum(1 for v in px.values() if v != (255, 255, 255))
            row["text_pixels_non_white"] = colored
            passed = passed and colored > 50
        window_bmp = work / "screens" / f"{name}-window.bmp"
        if not args.headless and window_bmp.exists() and shot.exists() and run.returncode != 3:
            sw, sh, spx, _ = decode_bmp(shot)
            cw, ch, cpx, _ = decode_bmp(window_bmp)
            f = cw // sw if sw else 0
            check = {"capture": str(window_bmp), "width": cw, "height": ch, "scale": f}
            if f < 1 or cw != sw * f or ch != sh * f:
                check["error"] = "window size is not an integer multiple of screen 0"
                passed = False
            else:
                diff = [(x, y) for y in range(sh) for x in range(sw) if cpx[(x * f, y * f)] != spx[(x, y)]]
                ov = exp.get("overlay")
                if ov:
                    # Visible child controls are composited at presentation only:
                    # differences must exist and stay inside the control rectangle.
                    x0, y0, w0, h0 = ov
                    outside = [d for d in diff if not (x0 <= d[0] < x0 + w0 and y0 <= d[1] < y0 + h0)]
                    check["overlay_pixels"] = len(diff)
                    check["mismatches"] = len(outside)
                    passed = passed and diff and not outside
                else:
                    check["mismatches"] = len(diff)
                    passed = passed and not diff
            row["window_capture"] = check
        if args.windows_reference and name in {**FIXTURES, **REFERENCE_FIXTURES}:
            refdir = args.windows_reference / name
            compared = []
            if refdir.is_dir():
                for ref in sorted(refdir.iterdir()):
                    if ref.suffix.lower() not in (".bmp", ".txt"):
                        continue
                    mine = scratch / ref.name
                    if ref.suffix.lower() == ".txt":
                        same = mine.exists() and mine.read_bytes().replace(b"\r\n", b"\n") == ref.read_bytes().replace(b"\r\n", b"\n")
                    else:
                        same = mine.exists() and mine.read_bytes() == ref.read_bytes()
                    compared.append({"file": ref.name, "identical": same})
                    passed = passed and same
            row["windows_reference"] = compared
        row["passed"] = passed
        ok = ok and passed
        results.append(row)
        refs = row.get("windows_reference")
        refnote = f" windows-identical={sum(r['identical'] for r in refs)}/{len(refs)}" if refs else ""
        wc = row.get("window_capture")
        if wc:
            refnote += f" window-capture x{wc.get('scale')} mismatches={wc.get('mismatches', wc.get('error'))}"
        print(f"{'PASS' if passed else 'FAIL'} {name}: exit={run.returncode} reason={rep.get('exit_reason')}{refnote} "
              f"unsupported={rep.get('first_unsupported','')!r}")
    (work / "results.json").write_text(json.dumps({"display": "headless" if args.headless else "window",
                                                   "results": results}, indent=2, ensure_ascii=False) + "\n")
    print(f"summary: {sum(r.get('passed', False) for r in results)}/{len(results)} -> {work / 'results.json'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
