#!/usr/bin/env python3
"""Verify a fetched work/windows/reference-build/gui-native-* run (see
windows_gui_run.py). Uses the expectation tables of tests/graphics/run_gui_fixtures.py,
compares output BMPs byte-for-byte with the official HSP 3.4 x86 GUI reference
(gui-fixtures-02), checks --window-capture against screen 0, decodes the
session-1 PrintWindow PNGs of the owned host window, and summarises the original
AX runs (title, new game + save, profile copy + load). Writes verify.json and PNG
previews under <run>/png/. Exit 1 if an automated check fails.
"""
import hashlib
import importlib.util
import json
import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
X86_REF = ROOT / "work/windows/reference-build/gui-fixtures-02/cases"


def load_table():
    spec = importlib.util.spec_from_file_location("run_gui_fixtures", ROOT / "tests/graphics/run_gui_fixtures.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read_bmp(path: Path):
    d = path.read_bytes()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    stride = ((w * bpp + 31) // 32) * 4
    rows = []
    for y in range(abs(h)):
        ry = abs(h) - 1 - y if h > 0 else y
        r = d[off + ry * stride: off + ry * stride + w * bpp // 8]
        rows.append([tuple(r[x * bpp // 8: x * bpp // 8 + 3][::-1]) for x in range(w)])
    return w, abs(h), rows


def read_png(path: Path):
    d = path.read_bytes()
    assert d[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, w = 8, b"", 0
    while pos < len(d):
        n = struct.unpack(">I", d[pos:pos + 4])[0]
        t = d[pos + 4:pos + 8]
        c = d[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", c[:10])
            assert depth == 8 and ctype in (2, 6)
            bpp = 3 if ctype == 2 else 4
        elif t == b"IDAT":
            idat += c
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[i]; line = bytearray(raw[i + 1:i + 1 + stride]); i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            cc = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - cc; pa, pb, pc = abs(p - a), abs(p - b), abs(p - cc)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else cc)) & 255
        rows.append([tuple(line[x * bpp: x * bpp + 3]) for x in range(w)])
        prev = line
    return w, h, rows


def write_png(rows, path: Path):
    h, w = len(rows), len(rows[0])
    raw = b"".join(b"\0" + bytes(v for px in r for v in px) for r in rows)
    c = lambda t, b: struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xffffffff)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + c(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                     c(b"IDAT", zlib.compress(raw)) + c(b"IEND", b""))


def compare_scaled(screen, capture, overlay=None):
    sw, sh, s = screen
    cw, ch, c = capture
    f = cw // sw if sw else 0
    if f < 1 or cw != sw * f or ch < sh * f:
        return {"error": f"capture {cw}x{ch} is not an integer multiple of screen {sw}x{sh}"}
    diff = [(x, y) for y in range(sh) for x in range(sw) if c[y * f][x * f] != s[y][x]]
    out = {"scale": f, "capture": f"{cw}x{ch}"}
    if overlay:
        x0, y0, w0, h0 = overlay
        outside = [p for p in diff if not (x0 <= p[0] < x0 + w0 and y0 <= p[1] < y0 + h0)]
        out.update(overlay_pixels=len(diff), mismatches=len(outside), ok=bool(diff) and not outside)
    else:
        out.update(mismatches=len(diff), ok=not diff)
    return out


def main() -> int:
    run = Path(sys.argv[1])
    mod = load_table()
    table = {**mod.FIXTURES, **mod.REFERENCE_FIXTURES, **mod.LOCAL_FIXTURES}
    rep = json.loads((run / "report.json").read_text(encoding="utf-8-sig"))
    (run / "png").mkdir(exist_ok=True)
    out = {"run": run.name, "build_exit": rep.get("buildExit"), "deps": rep.get("deps"), "font": rep.get("font"),
           "cmake_sdl": rep.get("cmakeSdl"), "backend": rep.get("backendLine"), "fixtures": {}, "games": {}}
    failed = rep.get("buildExit") != 0
    probe = (run / "window-results/probe.log").read_text(encoding="utf-8") if (run / "window-results/probe.log").exists() else ""
    for name, exp in table.items():
        row = {}
        for mode in ("headless", "window"):
            case = run / mode / name
            r = json.loads((case / "report.json").read_text(encoding="utf-8")) if (case / "report.json").exists() else {}
            m = {"exit": r.get("exit_code"), "reason": r.get("exit_reason"), "unsupported": r.get("first_unsupported"),
                 "display": r.get("display")}
            ok = r.get("exit_code") == exp["exit"]
            if "unsupported" in exp:
                ok = ok and exp["unsupported"] in (r.get("first_unsupported") or "")
            if "bmp" in exp and (case / exp["bmp"][0]).exists():
                fname, w, h, fn = exp["bmp"]
                bw, bh, px = read_bmp(case / fname)
                fn = fn or mod.pattern_expected
                bad = sum(1 for y in range(bh) for x in range(bw) if px[y][x] != fn(x, y))
                m["bmp"] = {"file": fname, "size": f"{bw}x{bh}", "mismatches": bad}
                ok = ok and (bw, bh) == (w, h) and bad == 0
            elif "bmp" in exp:
                ok = False; m["bmp"] = "missing"
            if exp.get("text") and (case / "screen0.bmp").exists():
                _, _, px = read_bmp(case / "screen0.bmp")
                m["text_pixels_non_white"] = sum(1 for rr in px for v in rr if v != (255, 255, 255))
                ok = ok and m["text_pixels_non_white"] > 50
            refdir = X86_REF / name
            if name in {**mod.FIXTURES, **mod.REFERENCE_FIXTURES} and refdir.is_dir():
                comp = []
                for ref in sorted(refdir.iterdir()):
                    if ref.suffix.lower() in (".bmp", ".txt"):
                        mine = case / ref.name
                        a = mine.read_bytes() if mine.exists() else None
                        same = a is not None and (a.replace(b"\r\n", b"\n") == ref.read_bytes().replace(b"\r\n", b"\n")
                                                  if ref.suffix.lower() == ".txt" else a == ref.read_bytes())
                        comp.append({"file": ref.name, "identical_to_x86": same})
                        ok = ok and same
                m["x86_reference"] = comp
            if mode == "window":
                if (case / "window.bmp").exists() and (case / "screen0.bmp").exists() and r.get("exit_code") != 3:
                    m["window_capture"] = compare_scaled(read_bmp(case / "screen0.bmp"), read_bmp(case / "window.bmp"), exp.get("overlay"))
                    ok = ok and m["window_capture"].get("ok", False)
                elif r.get("exit_code") != 3:
                    m["window_capture"] = {"error": "missing renderer readback or screen0"}
                    ok = False
                ok = ok and r.get("display") == "sdl3"
                pm = re.search(rf"^{re.escape(name)}\.visible_windows=(\d+)", probe, re.M)
                m["probe_visible_windows"] = int(pm.group(1)) if pm else None
                m["probe_exited_before_capture"] = bool(re.search(rf"^{re.escape(name)}\.exited_before_capture", probe, re.M))
                png = run / "window-results" / f"{name}-window0-pw.png"
                if png.exists() and (case / "screen0.bmp").exists():
                    m["printwindow_vs_screen0"] = compare_scaled(read_bmp(case / "screen0.bmp"), read_png(png), exp.get("overlay"))
            m["ok"] = ok
            failed |= not ok
            row[mode] = m
        out["fixtures"][name] = row
    for g in ("startup", "newgame", "load", "window-startup"):
        ev = run / "evidence" / f"game-{g}"
        r = json.loads((ev / "report.json").read_text(encoding="utf-8")) if (ev / "report.json").exists() else {}
        info = {k: r.get(k) for k in ("exit_code", "exit_reason", "frames", "first_unsupported", "hsp_error", "display")}
        saves = sorted((ev / "user/save").glob("*/*")) if (ev / "user/save").exists() else []
        info["save_files"] = len([p for p in saves if p.is_file()])
        info["save_bytes"] = sum(p.stat().st_size for p in saves if p.is_file())
        info["save_dirs"] = sorted({p.parent.name for p in saves})
        shots = sorted((ev / "shots").glob("*.bmp")) if (ev / "shots").exists() else []
        info["shots"] = [s.name for s in shots]
        for s in shots + [ev / "screen0.bmp", ev / "window.bmp"]:
            if s.exists():
                w, h, px = read_bmp(s)
                write_png(px, run / "png" / f"{g}-{s.stem}.png")
        out["games"][g] = info
        expected_exit = 0 if g == "newgame" else 124
        expected_reason = "end" if g == "newgame" else ("timeout" if g == "window-startup" else "max-waits")
        info["execution_ok"] = (r.get("exit_code") == expected_exit and
                                r.get("exit_reason") == expected_reason and
                                not r.get("first_unsupported") and not r.get("hsp_error"))
        if g in ("newgame", "load"):
            info["execution_ok"] = info["execution_ok"] and info["save_files"] > 100
        failed |= not info["execution_ok"]
    pm = re.findall(r"^window-startup\.(\S+)=(.*)$", probe, re.M)
    out["games"]["window-startup"]["probe"] = dict(pm)
    png = run / "window-results/window-startup-window0-pw.png"
    if png.exists():
        w, h, px = read_png(png)
        out["games"]["window-startup"]["printwindow_png"] = {"size": f"{w}x{h}",
                                                             "non_black": sum(1 for rr in px for v in rr if v != (0, 0, 0)),
                                                             "distinct_colors": len({v for rr in px for v in rr})}
    out["session"] = rep.get("session")
    out["original"] = rep.get("original")
    out["automated_fixture_checks_passed"] = all(v["ok"] for row in out["fixtures"].values() for v in row.values())
    session, original = out["session"] or {}, out["original"] or {}
    game_window = out["games"]["window-startup"]
    capture = game_window.get("printwindow_png", {})
    out["session_and_integrity_ok"] = (
        session.get("probeCompleted") is True and session.get("taskRemoved") is True and
        session.get("leftoverAfterCleanup") == 0 and original.get("zipUnchanged") is True and
        original.get("privateUnchanged") is True and
        int(game_window.get("probe", {}).get("visible_windows", "0").split()[0]) > 0 and
        capture.get("distinct_colors", 0) > 100 and game_window.get("display") == "sdl3")
    failed |= not out["session_and_integrity_ok"]
    out["all_automated_checks_passed"] = not failed
    (run / "verify.json").write_text(json.dumps(out, ensure_ascii=False, indent=1), encoding="utf-8")
    for name, row in out["fixtures"].items():
        print(name, *(f"{m}:{'PASS' if v['ok'] else 'FAIL'}(exit={v['exit']})" for m, v in row.items()))
    for g, v in out["games"].items():
        print(g, {k: v[k] for k in ("exit_code", "exit_reason", "frames", "first_unsupported", "save_files")})
    print("session", out["session"]); print("original", out["original"])
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
