#!/usr/bin/env python3
"""Compare fresh JP gameplay with/without managed Korean display hooks.

Requires previously compiled private reference and managed AX files. Uses new
profiles only; never imports or writes a player profile. Outputs may contain
private game/save data and must remain under work/.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from inspect_save_state import compare, inspect, manifest
from prepare_korean_game import check_output_path


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# Body rectangles in the 800x600 deterministic captures, excluding title, portrait and HUD.
# Separate references reflect the system fonts used by the native hosts. Hashes only:
# Mac japanese-gate-final-01, Windows gui-native-ko-managed-03.
NORNE_PAGES = ("norne-home-1", "norne-home-2", "norne-world-1", "norne-world-2",
               "norne-world-3", "norne-world-4")


def norne_body(path: Path) -> bytes:
    bmp = path.read_bytes()
    if bmp[:2] != b"BM" or len(bmp) < 54:
        raise ValueError("not a complete BMP header")
    offset = struct.unpack_from("<I", bmp, 10)[0]
    width, height, planes, bits, compression = struct.unpack_from("<iiHHI", bmp, 18)
    if (width, abs(height), planes, bits, compression) != (800, 600, 1, 24, 0):
        raise ValueError("expected an uncompressed 800x600 24-bit capture")
    stride = width * 3  # 800 BGR pixels occupy a whole number of four-byte words.
    if offset < 54 or len(bmp) < offset + abs(height) * stride:
        raise ValueError("truncated Norne capture")
    body = bytearray()
    for y in range(255, 377):
        row = offset + (599 - y if height > 0 else y) * stride
        body.extend(bmp[row + 276 * 3:row + 628 * 3])
    return bytes(body)


NORNE_BODY_SHA256 = {
    "macos": {
        "reference": (
            "2c4c438812efba45401fba8bae336c5df77f8b4200b2567f3515717400b525d5",
            "d633b13d74e552642b266b775ee298d8d82921c6f1ce9649e2a8f30d6c47ae50",
            "0e1ed1fa719a38bccd78a0281cf244ab28e5e7baecda1ad3d9af24b9d18cc8fb",
            "d2df7cc1511a1ced4c5938124bbb7ceb3a120d8670291dd1e6f94cd584419289",
            "f4baa0d53098d1bb885c16d0d7cd54c83f774f821ed06eb07aff5dfb83d5c0b3",
            "2c5b711bac7fa0a8847eec099b8740b6162818a0f0521e3ed419d3427bb23ca0",
        ),
        "korean": (
            "e4f018ef239cc531aa2a3e952fdc5715b8c972c9cc27f9b5b14ebd85f39b10f0",
            "d7d78d731f16ae48215ee127a32f9ab061b0ce2350c88f668553ff960023da97",
            "3d3e571e0e6d045e8545d16be24439decc53b2434b1d0ff45a34cffc8d52c035",
            "b90e928fc8db621491f98bed114c7b0f13001677607eaef4e3f8f1b8a2853eba",
            "1c5c6ea7636377a543f827f4544f1bbcec87cb0b1e5611c48ffeae409ab39e77",
            "939e1055ea5b78503bcd77f488832d0cd0e6b16a133079173532025a053fba7d",
        ),
    },
    "windows": {
        "reference": (
            "94b391eb582056e1a8051e10bfdf205799b7fbd6cb8e885606fe80e8f8c06946",
            "de1448d4b01c189f56921f58d0413a94ebb1c10a5605cfdd41164680f539fdec",
            "cde439b1e0298232fb01f44f4082052e031e1d164e08f6033d7131323468de62",
            "5aac4693d4324fbca6cd753bdbcf74a5357166d7e63375c5ecec16f8e709b14f",
            "96e5c0f7abf1438ad018c77164df9bc20f98ee555c9f4d93c73975760b92f7dd",
            "17ac569187bd5aa94bff8aee09439c9dd82c6d738f71abc920e36625c44475ab",
        ),
        "korean": (
            "6c8b9b2548029dc435970e836f7c1b3c13aadf478c9028e0e34b39b513016456",
            "3d889201d8afae78f0183d668b113e054d5ed2865a16e3f8953d32424f556979",
            "6fa78e06938888d933f32c4fa1d8484c3678bae1827dfd4054acc4a4de365af3",
            "96044cf873328b50472f846b18c72b7983eccff38b53c34f48c4b5c95a4222fe",
            "338895d7dcf5d76c4cddd4123473c66d9092d1930c965b285cded125f4932aa4",
            "191ea0a5b66d608ffdf916b5e2c958afdcbbcb61179a29bf9bdd63a54720830f",
        ),
    },
}


def verify_norne_pages(platform: str, capture_dirs: dict[str, Path]) -> dict:
    results = {}
    for i, page in enumerate(NORNE_PAGES):
        for variant, folder in capture_dirs.items():
            expected_variant = "reference" if variant == "fallback" else variant
            expected = NORNE_BODY_SHA256[platform][expected_variant][i]
            item = {"expected_body_sha256": expected, "passed": False}
            path = folder / (page + ".bmp")
            try:
                actual = hashlib.sha256(norne_body(path)).hexdigest()
                item.update(body_sha256=actual, passed=actual == expected)
                if variant == "fallback":
                    identical = path.read_bytes() == (capture_dirs["reference"] / path.name).read_bytes()
                    item.update(full_reference_equal=identical, passed=item["passed"] and identical)
            except (OSError, ValueError) as exc:
                item["error"] = str(exc)
            results[f"{variant}/{page}"] = item
    return results


def save_payloads(directory: Path) -> dict:
    result = {}
    for path in directory.rglob("*"):
        if not path.is_file():
            continue
        data = path.read_bytes()
        if data[:2] == b"\x1f\x8b":
            data = gzip.decompress(data)
        result[str(path.relative_to(directory))] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ("host", "reference-ax", "managed-ax", "catalog", "work"):
        ap.add_argument("--" + name, required=True, type=Path)
    args = ap.parse_args()
    destination = check_output_path(args.work)
    host, reference, managed, catalog = [p.resolve() for p in
                                        (args.host, args.reference_ax, args.managed_ax, args.catalog)]
    for path in (host, reference, managed, catalog):
        if not path.is_file():
            ap.error(f"input missing: {path}")
    destination.mkdir(parents=True)
    # Freeze the reviewed catalog while another agent may add more translations.
    private_catalog = destination / "catalog.tsv"
    shutil.copyfile(catalog, private_catalog)
    original = manifest(ROOT / "elonaplus2.335")
    zip_hash = sha(ROOT / "elonaplus2.335.zip")
    source = (ROOT / "tests/game/newgame_english.script").read_text()
    if source.count("120 key DOWN\n") != 1:
        raise ValueError("newgame script language-selection anchor changed")
    jp_script = destination / "newgame_japanese.script"
    jp_script.write_text(source.replace("120 key DOWN\n", ""))
    stages = [("newgame", jp_script, 4700),
              ("reload", ROOT / "tests/game/load_inspect_resave.script", 1400),
              ("world", Path(__file__).with_name("japanese_home_to_world.script"), 3200)]
    report = {"scope": "fresh JP native reference versus managed Korean display; deterministic headless",
              "host_sha256": sha(host), "reference_ax_sha256": sha(reference),
              "managed_ax_sha256": sha(managed), "catalog_sha256": sha(private_catalog),
              "runs": {}, "comparisons": {}, "passed": False}
    try:
        for variant, ax in (("reference", reference), ("fallback", managed), ("korean", managed)):
            run_root = destination / variant
            user = run_root / "user"
            user.mkdir(parents=True)
            report["runs"][variant] = {}
            for stage, script, waits in stages:
                run = run_root / stage
                (run / "cwd").mkdir(parents=True)
                command = [str(host), "--game", str(ax), "--game-root", str(ROOT / "elonaplus2.335"),
                           "--user-root", str(user), "--work-dir", str(run / "cwd"),
                           "--packed-dir", str(ROOT / "work/extracted/2.335"),
                           "--headless", "--deterministic", "--max-waits", str(waits),
                           "--timeout-ms", "120000", "--trace-commands", "--script", str(script),
                           "--report", str(run / "report.json"), "--screenshot", str(run / "screen0.bmp")]
                if variant == "korean":
                    command += ["--translation", str(private_catalog)]
                (run / "argv.json").write_text(json.dumps(command, indent=2) + "\n")
                with (run / "stdout.log").open("wb") as stdout, (run / "stderr.log").open("wb") as stderr:
                    process = subprocess.run(command, stdout=stdout, stderr=stderr, cwd=ROOT, timeout=150)
                host_report = json.loads((run / "report.json").read_text())
                slot = user / "save/sav_Ari"
                state = inspect(slot) if slot.is_dir() else None
                calls = host_report["command_counts"].get("native_ko_gmes", 0)
                checks = {"exit0": process.returncode == host_report["exit_code"] == 0,
                          "reason_end": host_report["exit_reason"] == "end",
                          "no_hsp_error": not host_report["hsp_error"] and not host_report.get("observed_hsp_error_count", 0),
                          "no_unsupported": not host_report["first_unsupported"], "saved": state is not None}
                if stage == "world":
                    checks["north_tyris"] = state is not None and state["location"]["gdata20"] == 4
                    checks["coordinates"] = state is not None and (state["player"]["x"], state["player"]["y"]) == (22, 21)
                    if variant == "korean":
                        checks["native_paragraphs_reached"] = calls == 12
                item = {"checks": checks, "state": state, "native_gmes_calls": calls,
                        "frames": host_report["frames"], "script_sha256": sha(script)}
                report["runs"][variant][stage] = item
                if slot.is_dir():
                    shutil.copytree(slot, run / "save_snapshot")
                print(variant, stage, checks, flush=True)
                if not all(checks.values()):
                    raise RuntimeError(f"{variant}/{stage} failed: {[k for k, v in checks.items() if not v]}")
        for stage, _, _ in stages:
            a = destination / "reference" / stage / "save_snapshot"
            b = destination / "korean" / stage / "save_snapshot"
            left, right = save_payloads(a), save_payloads(b)
            state_compare = compare(a, b)
            report["comparisons"][stage] = {
                "file_count_reference": len(left), "file_count_korean": len(right),
                "all_payloads_equal": left == right,
                "differences": sorted(k for k in left.keys() | right.keys() if left.get(k) != right.get(k)),
                "invariants": state_compare["invariants"], "turn_delta": state_compare["turn_counter_delta"],
                "display_changed": (destination / "reference" / stage / "screen0.bmp").read_bytes() !=
                                   (destination / "korean" / stage / "screen0.bmp").read_bytes(),
            }
        report["norne_pages"] = verify_norne_pages("macos", {
            variant: destination / variant / "world/cwd/shots" for variant in ("reference", "fallback", "korean")
        })
        report["passed"] = (all(r["all_payloads_equal"] for r in report["comparisons"].values())
                            and all(r["passed"] for r in report["norne_pages"].values()))
    except Exception as exc:
        report["error"] = str(exc)
    finally:
        report["original_tree_unchanged"] = original == manifest(ROOT / "elonaplus2.335")
        report["original_zip_unchanged"] = zip_hash == sha(ROOT / "elonaplus2.335.zip")
        report["host_unchanged"] = report["host_sha256"] == sha(host)
        report["passed"] = report["passed"] and all(report[k] for k in
                                                    ("original_tree_unchanged", "original_zip_unchanged", "host_unchanged"))
        (destination / "summary.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        print("PASS" if report["passed"] else "FAIL", destination / "summary.json")
        if report.get("error"):
            print(report["error"], file=sys.stderr)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
