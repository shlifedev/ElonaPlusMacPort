#!/usr/bin/env python3
"""Regression for the integrated elona_gui_host file boundary on the real VM.

Stage "fixtures": small CP932 fixtures (no game code) compiled with elona_hspcmp
and run with `elona_gui_host --game ... --headless --deterministic` where the
synthetic game root, user root, work dir and AX path all contain non-ASCII
characters. Checks exit/end code, generated config/user files (exact bytes),
the synthetic game root tree hash, and that the work dir stays empty. Cases
marked `gap` document a currently missing host boundary: the run reports
whether the gap is still present instead of failing the regression.

Stage "game": the normalized original AX on a private copy of the install under
a non-ASCII path, bounded headless/deterministic; records the first unsupported
call, generated user files and full-tree hashes of the original install and of
the private copy before/after. This is NOT a new-game or save/load verification.

Usage:
  python3 scripts/reference/host_regression.py --build build/host-regression-macos --run <name> [--game]
  python3 scripts/reference/host_regression.py --export DIR      # fixtures+expectations for Windows
"""
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/platform/host"
GAME_DIR_NAME = "原本 ゲーム root"
USER_DIR_NAME = "ユーザー データ"
WORK_DIR_NAME = "作業 cwd"
AX_DIR_NAME = "フィクスチャ 表"
PACKED_DIR_NAME = "DPM 展開"


def bmp_4x4() -> bytes:
    """4x4 24-bit bottom-up BMP, pixel (1,1) = RGB(12,34,56), others RGB(200,200,200)."""
    rows = []
    for y in range(3, -1, -1):
        row = b""
        for x in range(4):
            r, g, b = (12, 34, 56) if (x, y) == (1, 1) else (200, 200, 200)
            row += bytes((b, g, r))
        rows.append(row)          # 12 bytes per row: already 4-byte aligned
    pixels = b"".join(rows)
    header = b"BM" + struct.pack("<IHHI", 14 + 40 + len(pixels), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, 4, 4, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    return header + info + pixels


GAME_FILES = {
    "original/config.txt": b"cfg-default\r\n",
    "original/export.txt": b"export-default\r\n",
    "original/face1.bmp": b"BMface",
    "user/lastwords.txt": b"last",
    "user/graphic/keep.txt": b"k",
    "user/graphic2/keep.bmp": b"BMk",
    "save/readme.txt": b"r",
    "graphic/表.bmp": None,       # filled with bmp_4x4()
    "data/x.bin": b"x",
}
SCENE1 = "シーン1".encode("cp932")
SCENE2 = "シーン2".encode("cp932")


def crlf(s: str) -> bytes:
    return s.encode("cp932").replace(b"\n", b"\r\n")


EXPECTED_HOST_FILES_RESULT = crlf(
    "root=C:\\ElonaPlus\\\n"
    "exist_original=13\nconfig_before=-1\nconfig_after=13\nuser_export=16\nface=6\n"
    "ints=0,-1,-2147483648,305419896\n"
    "note=表ソA len=5\n"
    "dirlist_txt=3\nexport.txt\nlastwords.txt\n表.txt\n"
    "deleted=-1\n"
    "dirs=1 p1\n"
    "picload=12,34,56\n")

# name, kind (pass|gap), expected exit, expected user files {rel: bytes}, expected dirs, note
CASES = [
    ("host_files", "pass", 7, {
        "config.txt": b"cfg-default\r\n",
        "user/export.txt": b"export-default\r\n",
        "user/graphic/face1.bmp": b"BMface",
        "save/int.s1": bytes.fromhex("00000000" "ffffffff" "00000080" "78563412"),
        "user/表.txt": "表ソA".encode("cp932"),
        ".elonaplus/deleted.txt": b"user\\lastwords.txt\n",
        "user/host_result.txt": EXPECTED_HOST_FILES_RESULT,
    }, ["save/p1"], "bcopy original defaults, COW parent, int32 bsave/bload, CP932 trail-0x5C name, merged dirlist, tombstone, mkdir, picload via resolver"),
    ("host_dpm", "pass", 7, {"user/dpm_result.txt": crlf("dpm_len=%d\n" % len(SCENE1))}, [],
     "DPM member scene1.hsp by bare name via --packed-dir (non-ASCII directory)"),
    ("host_bmpsave_cow", "pass", 7, {"user/graphic2/shot.bmp": None}, [],
     "bmpsave into a game-root-only directory (begin/end_stream_write; gap closed in host source 2026-11-18 00:26)"),
    ("host_zopen_fresh", "pass", 7, {"user/zopen_result.txt": crlf("z=11,-22,2147483647,-2147483648\n")}, [],
     "zOpen write when user\\ is not yet in the user root (begin/end_stream_write; gap closed 2026-11-18)"),
    ("host_zopen_existing", "pass", 7, {"user/zopen_result.txt": crlf("z=11,-22,2147483647,-2147483648\n"),
                                        "user/materialize.txt": b"m"}, [],
     "zOpen write/read after user\\ was materialised"),
]


def tree_hash(root: Path):
    out = {}
    for p in sorted(root.rglob("*")):
        rel = p.relative_to(root).as_posix()
        if p.is_symlink():
            out[rel] = "<symlink>"
        elif p.is_dir():
            out[rel] = "<dir>"
        else:
            h = hashlib.sha256()
            with p.open("rb") as f:
                for chunk in iter(lambda: f.read(1 << 20), b""):
                    h.update(chunk)
            out[rel] = h.hexdigest()
    return out


def digest(tree) -> str:
    return hashlib.sha256(json.dumps(tree, sort_keys=True, ensure_ascii=False).encode()).hexdigest()


def make_game(game: Path):
    for rel, data in GAME_FILES.items():
        p = game / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(bmp_4x4() if data is None else data)


def cp932(name: str) -> bytes:
    return (FIXTURES / f"{name}.hsp").read_bytes().decode("utf-8").encode("cp932")


def export(directory: Path):
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)
    make_game(directory / "game")
    (directory / "packed").mkdir()
    (directory / "packed/scene1.hsp").write_bytes(SCENE1)
    (directory / "packed/scene2.hsp").write_bytes(SCENE2)
    manifest = {"game_dir_name": GAME_DIR_NAME, "user_dir_name": USER_DIR_NAME, "work_dir_name": WORK_DIR_NAME,
                "ax_dir_name": AX_DIR_NAME, "packed_dir_name": PACKED_DIR_NAME, "cases": []}
    for i, (name, kind, code, files, dirs, note) in enumerate(CASES):
        (directory / f"{name}.hsp").write_bytes(cp932(name))
        exp = []
        for j, (rel, data) in enumerate(files.items()):
            entry = {"path": rel}
            if data is not None:
                (directory / f"{name}.{j}.expected").write_bytes(data)
                entry["expected"] = f"{name}.{j}.expected"
            exp.append(entry)
        manifest["cases"].append({"name": name, "kind": kind, "exit": code, "files": exp, "dirs": dirs, "note": note})
    (directory / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=1), encoding="utf-8")


def run_host(host: Path, args, log_prefix: Path, timeout: int):
    t0 = time.time()
    r = subprocess.run([str(host)] + [str(a) for a in args], capture_output=True, timeout=timeout)
    log_prefix.with_suffix(".stdout.log").write_bytes(r.stdout)
    log_prefix.with_suffix(".stderr.log").write_bytes(r.stderr)
    return r, time.time() - t0


def fixtures_stage(build: Path, out: Path):
    host, compiler = build / "elona_gui_host", build / "elona_hspcmp"
    common = ROOT / ".cache/upstream/OpenHSP/common"
    game = out / GAME_DIR_NAME
    make_game(game)
    game_before = tree_hash(game)
    ax_dir = out / AX_DIR_NAME
    ax_dir.mkdir(parents=True)
    packed = out / PACKED_DIR_NAME
    packed.mkdir()
    (packed / "scene1.hsp").write_bytes(SCENE1)
    (packed / "scene2.hsp").write_bytes(SCENE2)
    results = []
    for name, kind, code, files, dirs, note in CASES:
        case = out / name
        user, work = case / USER_DIR_NAME, case / WORK_DIR_NAME
        work.mkdir(parents=True)
        src = ax_dir / f"{name}.hsp"
        src.write_bytes(cp932(name))
        ax = ax_dir / f"{name}.ax"
        c = subprocess.run([str(compiler), "-d", f"--compath={common}/", f"-o{ax}", str(src)], capture_output=True)
        row = {"name": name, "kind": kind, "note": note, "compile_exit": c.returncode}
        if c.returncode or not ax.exists():
            row.update(ok=False, problem="compile failed", compiler=c.stdout.decode("cp932", "replace"))
            results.append(row)
            continue
        args = ["--game", ax, "--game-root", game, "--user-root", user, "--work-dir", work, "--headless",
                "--deterministic", "--max-waits", "200", "--timeout-ms", "30000", "--report", case / "report.json"]
        args += ["--packed-dir", packed]
        r, secs = run_host(host, args, case / "host", 60)
        report = json.loads((case / "report.json").read_text(encoding="utf-8")) if (case / "report.json").exists() else {}
        problems = []
        if r.returncode != code:
            problems.append(f"exit {r.returncode} != {code}")
        for rel, data in files.items():
            p = user / rel
            if not p.is_file():
                problems.append(f"missing {rel}")
            elif data is not None and p.read_bytes() != data:
                problems.append(f"bytes differ {rel}: {p.read_bytes()[:120]!r}")
        for rel in dirs:
            if not (user / rel).is_dir():
                problems.append(f"missing dir {rel}")
        work_left = sorted(x.relative_to(work).as_posix() for x in work.rglob("*"))
        if work_left:
            problems.append(f"work dir not empty: {work_left}")
        row.update(exit=r.returncode, seconds=round(secs, 2), exit_reason=report.get("exit_reason"),
                   first_unsupported=report.get("first_unsupported"), hsp_error=report.get("hsp_error"),
                   problems=problems, stderr_tail=r.stderr.decode("utf-8", "replace").strip().splitlines()[-3:])
        if kind == "pass":
            row["ok"] = not problems
        else:
            row["gap_still_present"] = bool(problems)
            row["ok"] = True      # a documented gap does not fail the regression
        results.append(row)
    game_after = tree_hash(game)
    return {"synthetic_game_tree_unchanged": game_before == game_after,
            "synthetic_game_tree_sha": digest(game_before), "cases": results}


def game_stage(build: Path, out: Path, max_waits: int, timeout_ms: int, script=None):
    host = build / "elona_gui_host"
    original = ROOT / "elonaplus2.335"
    ax_src = ROOT / "work/normalized/2.335/start.ax"
    t0 = time.time()
    original_before = tree_hash(original)
    zip_before = hashlib.sha256((ROOT / "elonaplus2.335.zip").read_bytes()).hexdigest()
    private = out / "原本 ゲーム 2.335"
    shutil.copytree(original, private, symlinks=True)
    private_before = tree_hash(private)
    if private_before != original_before:
        raise SystemExit("private copy differs from the original install")
    ax_dir = out / AX_DIR_NAME
    ax_dir.mkdir(parents=True, exist_ok=True)
    ax = ax_dir / "start 表.ax"
    shutil.copyfile(ax_src, ax)
    user, work = out / "game" / USER_DIR_NAME, out / "game" / WORK_DIR_NAME
    work.mkdir(parents=True)
    packed = out / "game" / PACKED_DIR_NAME
    packed.mkdir()
    for name in ("scene1.hsp", "scene2.hsp"):
        shutil.copyfile(ROOT / "work/extracted/2.335" / name, packed / name)
    args = ["--game", ax, "--game-root", private, "--packed-dir", packed, "--user-root", user, "--work-dir", work, "--headless",
            "--deterministic", "--max-waits", str(max_waits), "--timeout-ms", str(timeout_ms),
            "--report", out / "game/report.json", "--screenshot", out / "game/screen0.bmp", "--trace-commands"]
    if script:
        args += ["--script", Path(script).resolve()]
    r, secs = run_host(host, args, out / "game/host", timeout_ms // 1000 + 60)
    report = json.loads((out / "game/report.json").read_text(encoding="utf-8")) if (out / "game/report.json").exists() else {}
    private_after = tree_hash(private)
    original_after = tree_hash(original)
    zip_after = hashlib.sha256((ROOT / "elonaplus2.335.zip").read_bytes()).hexdigest()
    generated = {}
    for p in sorted(user.rglob("*")):
        if p.is_file():
            rel = p.relative_to(user).as_posix()
            entry = {"bytes": p.stat().st_size, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
            default = private / "original" / p.name
            if default.is_file():
                entry["equals_original_default"] = default.read_bytes() == p.read_bytes()
            generated[rel] = entry
    return {
        "scope": "bounded headless deterministic diagnostic run; not new game / save / load",
        "script": str(script) if script else None,
        "command_counts": report.get("command_counts"),
        "exit": r.returncode, "seconds": round(secs, 2), "max_waits": max_waits, "timeout_ms": timeout_ms,
        "exit_reason": report.get("exit_reason"), "frames": report.get("frames"),
        "first_unsupported": report.get("first_unsupported"), "hsp_error": report.get("hsp_error"),
        "notes": report.get("notes"),
        "generated_user_files": generated,
        "work_dir_entries": sorted(x.relative_to(work).as_posix() for x in work.rglob("*")),
        "original_tree_files": len(original_before), "original_tree_sha": digest(original_before),
        "original_tree_unchanged": original_before == original_after,
        "original_zip_sha256": zip_before, "original_zip_unchanged": zip_before == zip_after,
        "private_copy_unchanged": private_before == private_after,
        "private_copy_changes": sorted(k for k in set(private_before) | set(private_after)
                                       if private_before.get(k) != private_after.get(k))[:20],
        "total_seconds": round(time.time() - t0, 1),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", type=Path, default=ROOT / "build/host-regression-macos")
    ap.add_argument("--run", default="host-01")
    ap.add_argument("--game", action="store_true", help="also run the bounded original-AX stage")
    ap.add_argument("--max-waits", type=int, default=400)
    ap.add_argument("--timeout-ms", type=int, default=120000)
    ap.add_argument("--script", help="deterministic input script for the game stage (e.g. tests/game/startup_english.script)")
    ap.add_argument("--export", type=Path)
    args = ap.parse_args()
    if args.export:
        export(args.export.resolve())
        print(f"exported to {args.export}")
        return 0
    out = ROOT / "work/host-regression" / args.run
    if out.exists():
        raise SystemExit(f"run exists: {out}")
    out.mkdir(parents=True)
    report = {"build": str(args.build), "host_sha256": hashlib.sha256((args.build / "elona_gui_host").read_bytes()).hexdigest(),
              "platform": sys.platform, "fixtures": fixtures_stage(args.build.resolve(), out)}
    if args.game:
        report["game"] = game_stage(args.build.resolve(), out, args.max_waits, args.timeout_ms, args.script)
        shutil.rmtree(out / "原本 ゲーム 2.335")      # private copy: hashes recorded, bytes not kept
    (out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    ok = report["fixtures"]["synthetic_game_tree_unchanged"] and all(c["ok"] for c in report["fixtures"]["cases"])
    if args.game:
        g = report["game"]
        ok = ok and g["original_tree_unchanged"] and g["original_zip_unchanged"] and g["private_copy_unchanged"]
    for c in report["fixtures"]["cases"]:
        state = ("PASS" if c["ok"] else "FAIL") if c["kind"] == "pass" else ("GAP" if c.get("gap_still_present") else "GAP-CLOSED")
        print(f"{state} {c['name']} exit={c.get('exit')} {c.get('problems')} {c.get('first_unsupported') or ''}")
    print("synthetic game tree unchanged:", report["fixtures"]["synthetic_game_tree_unchanged"])
    if args.game:
        g = report["game"]
        print("game:", {k: g[k] for k in ("exit", "exit_reason", "frames", "first_unsupported", "original_tree_unchanged",
                                         "original_zip_unchanged", "private_copy_unchanged", "work_dir_entries")})
        print("generated:", json.dumps(g["generated_user_files"], ensure_ascii=False))
    print("OK" if ok else "FAILED", out / "report.json")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
