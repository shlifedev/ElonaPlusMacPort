#!/usr/bin/env python3
"""Verify a fetched gui-* reference run: exit codes, decoded BMP pixels, pget dump,
PrintWindow client captures and input-control observations. Writes verify.json.

Pass/fail is about decoded pixel bytes and dimensions, never just exit 0 or
PrintWindow success. Prints a summary; returns 1 if any automated check failed.
"""
import json
import re
import struct
import sys
from pathlib import Path


def read_bmp(path: Path):
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError("not BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    header = struct.unpack_from("<I", data, 14)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    planes, bpp, compression = struct.unpack_from("<HHI", data, 26)
    if compression not in (0,):
        raise ValueError(f"unsupported compression {compression}")
    top_down = height < 0
    h = abs(height)
    stride = ((width * bpp + 31) // 32) * 4
    palette = []
    if bpp <= 8:
        count = struct.unpack_from("<I", data, 46)[0] or (1 << bpp)
        base = 14 + header
        palette = [tuple(data[base + i * 4: base + i * 4 + 3][::-1]) for i in range(count)]
    pixels = []
    for y in range(h):
        row_index = y if top_down else h - 1 - y
        row = data[offset + row_index * stride: offset + (row_index + 1) * stride]
        line = []
        for x in range(width):
            if bpp == 24:
                b, g, r = row[x * 3: x * 3 + 3]
            elif bpp == 32:
                b, g, r = row[x * 4: x * 4 + 3]
            elif bpp == 8:
                r, g, b = palette[row[x]]
            else:
                raise ValueError(f"unsupported bpp {bpp}")
            line.append((r, g, b))
        pixels.append(line)
    return {"width": width, "height": h, "bpp": bpp, "top_down": top_down, "bytes": len(data),
            "header_size": header}, pixels


def pattern(x, y):
    return (x * 17, y * 17, (x + y) * 8)


def expected_pattern_screen(x, y):
    white = (255, 255, 255)
    if y < 16 and x < 16:
        return pattern(x, y)
    if y < 16 and x < 32:
        p = pattern(x - 16, y)
        return white if p == (0, 0, 0) else p
    if 16 <= y < 48 and x < 32:
        return pattern(x // 2, (y - 16) // 2)
    return white


EXPECT = {
    "fixture-buffer.bmp": (8, 8, lambda x, y: (17, 34, 51)),
    "pr-buffer1-before.bmp": (16, 16, lambda x, y: (17, 34, 51)),
    "pr-screen0.bmp": (16, 16, lambda x, y: (17, 34, 51)),
    "pr-buffer1-after.bmp": (16, 16, lambda x, y: (17, 34, 51)),
    "ck-buffer1.bmp": (16, 16, lambda x, y: (255, 0, 0) if x < 8 else (0, 0, 0)),
    "ck-screen0.bmp": (16, 16, lambda x, y: (255, 0, 0) if x < 8 else (0, 0, 255)),
    "pt-buffer1.bmp": (16, 16, pattern),
    "pt-screen0.bmp": (48, 48, expected_pattern_screen),
}
EXIT = {"pixel_roundtrip": "0", "color_key": "0", "bitmap_export": "0"}


def check_pixels(width, height, pixels, expect, w, h):
    if (width, height) != (w, h):
        return {"ok": False, "reason": f"size {width}x{height} != {w}x{h}"}
    bad = [(x, y, pixels[y][x], expect(x, y)) for y in range(h) for x in range(w) if pixels[y][x] != expect(x, y)]
    return {"ok": not bad, "mismatches": len(bad), "first_mismatches": [list(map(list, [b[2], b[3]])) + [b[0], b[1]] for b in bad[:8]]}


def main() -> int:
    run = Path(sys.argv[1])
    results = run / "results"
    log = (results / "probe.log").read_text(encoding="utf-8") if (results / "probe.log").exists() else ""
    kv = {}
    for line in log.splitlines():
        for m in re.finditer(r"([A-Za-z0-9_.-]+)=(\S*)", line):
            kv.setdefault(m.group(1), m.group(2))
    report = {"run": run.name, "probe_log_present": bool(log), "checks": {}}
    failed = False
    for name, code in EXIT.items():
        got = kv.get(f"{name}.exit_code")
        ok = got == code
        report["checks"][f"{name}.exit"] = {"ok": ok, "observed": got, "expected": code}
        failed |= not ok
    for bmp, (w, h, fn) in EXPECT.items():
        found = list((run / "cases").glob(f"*/{bmp}"))
        if not found:
            report["checks"][bmp] = {"ok": False, "reason": "missing"}
            failed = True
            continue
        info, pixels = read_bmp(found[0])
        result = check_pixels(info["width"], info["height"], pixels, fn, w, h)
        result["bmp"] = info
        result["path"] = str(found[0].relative_to(run))
        report["checks"][bmp] = result
        failed |= not result["ok"]
    pget = list((run / "cases").glob("*/pt-pget.txt"))
    if pget:
        rows = []
        ok = True
        for line in pget[0].read_bytes().decode("cp932").splitlines():
            if not line.strip():
                continue
            x, y, r, g, b = map(int, line.split(","))
            exp = expected_pattern_screen(x, y)
            rows.append({"x": x, "y": y, "rgb": [r, g, b], "expected": list(exp)})
            ok &= (r, g, b) == exp
        report["checks"]["pt-pget.txt"] = {"ok": ok and len(rows) == 9, "rows": rows}
        failed |= not (ok and len(rows) == 9)
    else:
        report["checks"]["pt-pget.txt"] = {"ok": False, "reason": "missing"}
        failed = True
    # PrintWindow client captures are observations: compare with the screen BMP if sizes match.
    captures = {}
    for case, bmp in (("gui_pixel_dump", "pr-screen0.bmp"), ("gui_colorkey_dump", "ck-screen0.bmp"), ("gui_pattern_dump", "pt-screen0.bmp")):
        size = kv.get(f"{case}-window0.client_size")
        entry = {"client_size": size, "dpi": kv.get(f"{case}-window0.dpi")}
        for suffix, method in (("", "pw"), ("-pwlegacy", "pwlegacy"), ("-dc", "dc")):
            raw = results / f"{case}-window0{suffix}.bgra"
            m = {"api_success": kv.get(f"{case}-window0{suffix}.capture_method") and
                 kv.get(f"{case}-window0{suffix}.capture_method") == method and None}
            line = re.search(rf"{re.escape(case)}-window0{re.escape(suffix)}\.capture_method={method} api_success=(\S+)", log)
            m["api_success"] = line.group(1) if line else kv.get(f"{case}-window0.printwindow") if method == "pw" else None
            if raw.exists() and size:
                cw, ch = map(int, size.split("x"))
                data = raw.read_bytes()
                w, h, fn = EXPECT[bmp]
                region_ok = cw >= w and ch >= h
                mismatches = 0
                if region_ok:
                    for y in range(h):
                        for x in range(w):
                            i = (y * cw + x) * 4
                            if (data[i + 2], data[i + 1], data[i]) != fn(x, y):
                                mismatches += 1
                m.update({"top_left_region_matches_expected": region_ok and mismatches == 0,
                          "mismatches": mismatches if region_ok else None})
            else:
                m["present"] = False
            entry[method] = m
        captures[case] = entry
    report["window_captures_observation"] = captures
    report["input_control_observation"] = {k: v for k, v in kv.items() if k.startswith("input_control")}
    report["input_control_note"] = ("Programmatic WM_SETTEXT/WM_GETTEXT/EM_* checks only. IME composition, "
                                    "commit/cancel, backspace and CP932 byte limits by keyboard are NOT verified.")
    report["automated_checks_passed"] = not failed
    (run / "verify.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for key, value in report["checks"].items():
        print(f"{'PASS' if value.get('ok') else 'FAIL'} {key} {value.get('reason', '')}{value.get('observed', '') or ''}")
    print(json.dumps(captures, indent=1))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
