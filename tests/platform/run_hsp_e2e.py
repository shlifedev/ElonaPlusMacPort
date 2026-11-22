#!/usr/bin/env python3
"""Compile the platform HSP fixtures (CP932) and run them on the real HSP VM with
the GameFileSystem file-event adapter. Checks exact stdout bytes, exit code, the
resulting user-root files, and that the synthetic game root is byte-identical.
No original game data is used.

--export DIR writes the same fixtures/expectations for hosts without Python
(scripts/reference/platform_build_x64.ps1 consumes them on Windows)."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
VR = "C:\\ElonaPlus\\"
GAME_FILES = {"original/config.txt": b"cfg", "user/lastwords.txt": b"last", "save/readme.txt": b"r",
              "graphic/Item.bmp": b"BM", "data/x.bin": b"x"}
PACKED = ("scene1.hsp", "シーン".encode("cp932"))
EXPECTED_FILE_OPS = b"".join([
    b"root=" + VR.encode() + b"\n",
    b"exist_original=3\n", b"exist_config_before=-1\n", b"exist_config_after=3\n",
    b"int_size=16\n",
    b"ints=0,-1,-2147483648,305419896\n",
    b"note=" + "表ソA".encode("cp932") + b" len=5\n",
    b"dirlist_txt=2\n", b"lastwords.txt\n" + "表.txt".encode("cp932") + b"\n",
    b"deleted=-1\n",
    b"dirs=1 p1\n",
    b"files=2\n",
    b"packed=" + PACKED[1] + b"\n",
    b"double_sep=2\n",
    b"patched=0,-65536\n",
])
# name, exit code, exact stdout (CRLF normalised), stderr substring
CASES = [
    ("file_ops", 7, EXPECTED_FILE_OPS, b""),
    ("file_denied", 1, b"before\n", b"not_writable"),
    ("file_chdir", 1, b"", b"chdir"),
]
# Files the file_ops case must leave in its user root (relative, '/'), exact bytes.
USER_FILES_FILE_OPS = {
    "config.txt": b"cfg",
    "user/表.txt": "表ソA".encode("cp932"),
    ".elonaplus/deleted.txt": b"user\\lastwords.txt\n",
    "save/int.s1": bytes.fromhex("0000" "0000" "0000" "ffff" "00000080" "78563412"),
}
USER_DIRS_FILE_OPS = ["save/p1"]


def tree(root: Path):
    return {str(p.relative_to(root)): (hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else "<dir>")
            for p in sorted(root.rglob("*"))}


def read(path: Path):
    return path.read_bytes() if path.is_file() else None


def make_game(game: Path):
    for rel, data in GAME_FILES.items():
        (game / rel).parent.mkdir(parents=True, exist_ok=True)
        (game / rel).write_bytes(data)


def cp932_source(name: str) -> bytes:
    return (HERE / "hsp" / f"{name}.hsp").read_bytes().decode("utf-8").encode("cp932")


def export(directory: Path):
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)
    make_game(directory / "game")
    (directory / PACKED[0]).write_bytes(PACKED[1])
    cases = []
    for name, code, out, err in CASES:
        (directory / f"{name}.hsp").write_bytes(cp932_source(name))
        (directory / f"{name}.expected.stdout").write_bytes(out)
        cases.append({"name": name, "exit": code, "stderr_contains": err.decode()})
    user_files = []
    for i, (rel, data) in enumerate(USER_FILES_FILE_OPS.items()):
        (directory / f"userfile{i}.expected").write_bytes(data)
        user_files.append({"path": rel, "expected": f"userfile{i}.expected"})
    manifest = {"cases": cases, "packed": PACKED[0], "user_files_file_ops": user_files,
                "user_dirs_file_ops": USER_DIRS_FILE_OPS}
    (directory / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"exported {len(cases)} cases to {directory}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--compiler")
    ap.add_argument("--host")
    ap.add_argument("--common")
    ap.add_argument("--work", type=Path)
    ap.add_argument("--export", type=Path)
    args = ap.parse_args()
    if args.export:
        export(args.export.resolve())
        return 0
    work = args.work.resolve()
    if work.exists():
        shutil.rmtree(work)
    (work / "ax").mkdir(parents=True)
    game = work / "game root ゲーム"
    make_game(game)
    (work / PACKED[0]).write_bytes(PACKED[1])
    before = tree(game)
    failures = []
    users = {}
    for name, code, expect_out, expect_err in CASES:
        src = work / "ax" / f"{name}.hsp"
        src.write_bytes(cp932_source(name))
        ax = src.with_suffix(".ax")
        c = subprocess.run([args.compiler, "-d", f"--compath={args.common}/", f"-o{ax}", str(src)], capture_output=True)
        if c.returncode or not ax.exists():
            raise SystemExit(f"compile failed {name}: {c.stdout!r} {c.stderr!r}")
        user = work / f"user-{name}"
        users[name] = user
        r = subprocess.run([args.host, str(ax), str(game), str(user), PACKED[0], str(work / PACKED[0])],
                           capture_output=True, timeout=60)
        out = r.stdout.replace(b"\r\n", b"\n")
        ok = r.returncode == code and out == expect_out and expect_err in r.stderr
        print(f"{'PASS' if ok else 'FAIL'} {name} exit={r.returncode}")
        if not ok:
            failures.append(name)
            print("stdout:", out.decode("cp932", "replace"))
            print("stderr:", r.stderr.decode("utf-8", "replace"))
            print("expected:", expect_out.decode("cp932", "replace"))
    user = users["file_ops"]
    for rel, data in USER_FILES_FILE_OPS.items():
        ok = read(user / rel) == data
        print(f"{'PASS' if ok else 'FAIL'} user file {rel}")
        if not ok:
            failures.append(rel)
    for rel in USER_DIRS_FILE_OPS:
        if not (user / rel).is_dir():
            failures.append(rel)
    ok = tree(game) == before
    print(f"{'PASS' if ok else 'FAIL'} original intact")
    if not ok:
        failures.append("original changed")
    print("FAILED" if failures else "OK", failures)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
