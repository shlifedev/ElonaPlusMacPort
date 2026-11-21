#!/usr/bin/env python3
"""Generate an opt-in display-hook source from the verified private 2.335 source.

The original CP932 bytes are never decoded/re-encoded. Output remains private
under work/. The Korean launcher selects the verified managed AX. This requires the
native type-48 display command; the official Windows runtime cannot run it.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

from korean_static_dialogue import static_dialogue_inserts
from korean_option_hooks import option_inserts
from korean_creation_hooks import creation_inserts
from korean_event_hooks import story_inserts
from korean_inventory_hooks import install_item_hooks
from korean_menu_hooks import menu_inserts
from korean_manual_hooks import manual_inserts
from korean_card_hooks import card_inserts
from korean_evolution_hooks import install_evolution_hooks
from korean_fixed_hooks import fixed_inserts
from korean_recipe_hooks import recipe_inserts
from korean_altar_hooks import install_altar_hooks


ROOT = Path(__file__).resolve().parents[1]
SOURCE_SHA256 = "c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55"
COMMAND_TYPE = 48
COMMAND_ID = 0
DECLARATIONS = b"#regcmd 48\r\n#cmd native_ko_gmes $00\r\n#cmd native_ko_log $01\r\n#cmd native_ko_hud $02\r\n#cmd native_ko_item $06\r\n#cmd native_ko_menu $07\r\n#cmd native_ko_board $08\r\n#cmd native_ko_book $09\r\n#cmd native_ko_input_caption $0a\r\n#cmd native_ko_label_width $0b\r\n"
ANCHOR = (b"#deffunc gmes str gmes_prm0\r\n"
          b'\tm@m111 = ""\r\n'
          b'\tmsg@m111 = "" + gmes_prm0 + "$end"\r\n')
ENTRY = b"#deffunc gmes str gmes_prm0\r\n"
HOOK = (b"\tnative_ko_gmes gmes_prm0, gmesx, gmesy, gmesw, gmestype, gmescol, gmescol(1), gmescol(2)\r\n"
        b"\tif ( stat >= 0 ) {\r\n"
        b"\t\tgmesy += stat\r\n"
        b"\t\treturn 0\r\n"
        b"\t}\r\n")


LOG_ENTRY = b'\t\tmsgtemp += " "\r\n\t\trepeat\r\n\t\t\tawait\r\n'
HUD_LINES = [
    (b'\tbmes "" + cdata(30, 0) + " gp", 255, 255, 255\r\n', 1),
    (b'\tbmes "" + cdata(31, 0) + " pp", 255, 255, 255\r\n', 1),
    (b'\tbmes "Sp" + cdata(52, 0) + "/" + cdata(53, 0), 255, 255, 255\r\n', 1),
    (b'\tbmes "Lv" + cdata(38, 0) + "/" + (cdata(36, 0) - cdata(35, 0)), 255, 255, 255\r\n', 1),
    (b'\t\tbmes _time(gdata(13) / 4) + " " + _weather(gdata(17)), 255, 255, 255\r\n', 1),
    (b'\t\t\tbmes _biyori(gdata(250 + 420)) + " / " + _destiny(gdata(250 + 428)), 255, 255, 255\r\n', 2),
]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


DIALOG_DRAW = (b"\tnotesel buff\r\n"
               b"\tif ( evochat < 2 | evochat == 11 ) {\r\n"
               b"\t\trepeat noteinfo(0)\r\n")


def dialogue_calls(source):
    """Mark only authored, reviewed NPC lines. Player and mod text never sets a key."""
    lines = source.splitlines(keepends=True)
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + len(line))
    rows = json.loads((ROOT / "localization/ko-KR/original-tutorial.json").read_text(encoding="utf-8"))
    for row in rows:
        if row["review_status"] != "reviewed" or row["match_kind"] != "exact":
            continue
        for ref in row["source_ref"]:
            if not ref.startswith("2.335:start.hsp:"):
                continue
            index = int(ref.rsplit(":", 1)[1]) - 1
            if not lines[index].lstrip().startswith(b"buff = "):
                continue
            key = row["source_ja"].replace('"', '\\"').encode("cp932")
            yield offsets[index + 1], (b'\tnative_dialog_original = buff\r\n'
                                       b'\tnative_dialog_pending = "' + key + b'"\r\n'), "dialogue_line_identity"


def transform(source: bytes) -> tuple[bytes, list[dict]]:
    """Hash-pinned, insertion-only transform; unknown/changed inputs fail closed."""
    if digest(source) != SOURCE_SHA256:
        raise ValueError("source SHA-256 differs from the verified 2.335 reference")
    if source.count(ANCHOR) != 1 or source.count(ENTRY) != 1:
        raise ValueError("gmes entry/context anchor must occur exactly once")
    if b"native_ko_gmes" in source or b"#regcmd" in source:
        raise ValueError("input already declares native commands; review type ownership")
    inserts = [(0, DECLARATIONS, "declare_native_commands"),
               (source.index(ENTRY) + len(ENTRY), HOOK, "gmes_display_boundary")]
    def at(line, addition, kind, *, after=False, count=1):
        if source.count(line) != count:
            raise ValueError(f"{kind} anchor count differs")
        start = 0
        for _ in range(count):
            pos = source.index(line, start)
            start = pos + len(line)
            inserts.append((start if after else pos, addition, kind))
    at(b"gcopy 3, 128, 288, 128, 32\r\n", b"\tnative_ko_input_caption\r\n",
       "input_banner", after=True, count=3)
    at(b"\t\ttx@m72 = limit(strlen(cs_list_prm0) * 7 + 32 + cs_list_prm4, 10, 480)\r\n",
       b"\t\tif ( native_alias_width > 0 ) : tx@m72 = limit(native_alias_width + 32 + cs_list_prm4, tx@m72, 480)\r\n"
       b"\t\tif ( native_manual_width > 0 ) : tx@m72 = limit(native_manual_width + 32, 10, 148)\r\n",
       "translated_selection_highlight", after=True)
    install_item_hooks(at)
    install_evolution_hooks(at, source)
    install_altar_hooks(at)
    inserts.extend(menu_inserts(source))
    inserts.extend(recipe_inserts(source))
    inserts.extend(manual_inserts(source))
    inserts.extend(card_inserts(source))
    inserts.extend(option_inserts(source))
    inserts.extend(creation_inserts(source))
    inserts.extend(story_inserts(source))
    inserts.extend(dialogue_calls(source))
    inserts.extend(static_dialogue_inserts(source, {offset for offset, _, _ in inserts}))
    inserts.extend(fixed_inserts(source, {offset for offset, _, _ in inserts}))
    at(b"\tnoteget buff2, p\r\n", b'\tnative_board_row = buff2\r\n'
       b'\tnative_board_section = s + "," + lang("JP", "EN")\r\n', "board_identity", after=True)
    title_draw = b"\t\tcs_list s(3), wx + 96, y - 1, 19\r\n"
    at(title_draw, b"\t\tnative_ko_board native_board_section, native_board_row, s(4), s(10), s(5), s(12), s(11), qdata(11, rq)\r\n", "board_prepare")
    at(title_draw, b'\t\tnative_ko_menu 0, ""\r\n', "board_title_end", after=True)
    body_draw = b"\t\tpos wx + 20, y + 20\r\n\t\tmes buff\r\n"
    at(body_draw, b'\t\tnative_ko_gmes "@board/body", wx + 20, y + 20, ww - 40, 0, 0, 0, 0\r\n'
       b'\t\tif ( stat >= 0 ) {\r\n\t\t\tgoto *native_board_body_done\r\n\t\t}\r\n', "board_draw")
    at(body_draw, b"*native_board_body_done\r\n", "board_body_end", after=True)
    at(b"\ttalk_conv buff, 56 - en * 3\r\n",
       b'\tnative_dialog_key = ""\r\n'
       b'\tif ( buff == str(native_dialog_original) ) {\r\n'
       b'\t\tnative_dialog_key = str(native_dialog_pending)\r\n\t}\r\n'
       b'\tnative_dialog_original = ""\r\n\tnative_dialog_pending = ""\r\n', "before_dialogue_wrap")
    at(DIALOG_DRAW, b'\tif ( evochat < 2 | evochat == 11 ) {\r\n'
       b'\t\tif ( strlen(native_dialog_key) > 1 ) {\r\n'
       b'\t\t\tnative_ko_gmes native_dialog_key, wx + 150, wy + 43, 425, 0, 20, 10, 5\r\n'
       b'\t\t\tif ( stat >= 0 ) {\r\n\t\t\t\tnotesel buff\r\n\t\t\t\treturn\r\n\t\t\t}\r\n'
       b'\t\t}\r\n\t}\r\n', "dialogue_draw")
    book_window = b"\twy = winposy(468)\r\n\tpos wx, wy\r\n\tgcopy 4, 0, 0, 736, 448\r\n"
    at(book_window, b"\tnative_ko_book inv(25, ci), buff, page, wx + 80, wy + 45\r\n"
       b"\tnative_book_drawn = stat\r\n", "book_spread", after=True)
    book_footer = (b"\t\tif ( p \\ 20 == 0 ) {\r\n"
                   b"\t\t\tfont lang(cfg_font1, cfg_font2), 12 + sizefix - en * 2, 1\r\n"
                   b"\t\t\tpos x + 90, y + 330\r\n"
                   b'\t\t\tmes "- " + (p / 20 + 1) + " -"\r\n\t\t}\r\n\tloop\r\n*label_5808\r\n')
    book_text = b"\t\tpos x, y\r\n\t\tmes s\r\n" + book_footer
    at(book_text, b'\t\tif ( native_book_drawn == 0 ) {\r\n\t\t\tnative_ko_menu 10, s\r\n', "book_original_begin")
    at(book_footer, b'\t\t\tnative_ko_menu 0, ""\r\n\t\t}\r\n', "book_original_end")
    # Copy only the presentation input, after English capitalization and before
    # the original loop destructively consumes one word at a time.
    at(LOG_ENTRY, b"\t\tnative_ko_log msgtemp, msglen, inf_maxmsglen, native_starter_pet_id, str(native_log_identity)\r\n", "english_log_boundary")
    jp_log = b'\t\trepeat\r\n\t\t\tlen@txtfunc = strlen(msgtemp)\r\n'
    at(jp_log, b"\t\tnative_ko_log msgtemp, msglen, inf_maxmsglen, native_starter_pet_id, str(native_log_identity), 1\r\n",
       "japanese_log_boundary")
    for line, count in HUD_LINES:
        at(line, b"\tnative_ko_hud 1\r\n", "hud_begin", count=count)
        at(line, b"\tnative_ko_hud 0\r\n", "hud_end", after=True, count=count)
    output = bytearray()
    changes = []
    previous = 0
    for offset, added, kind in sorted(inserts, key=lambda item: item[0]):
        output.extend(source[previous:offset])
        changes.append({"kind": kind, "input_byte_offset": offset, "output_byte_offset": len(output),
                        "inserted_bytes": len(added), "inserted_lines": added.count(b"\n"),
                        "sha256": digest(added)})
        output.extend(added)
        previous = offset
    output.extend(source[previous:])
    restored = bytearray(output)
    for change in reversed(changes):
        start = change["output_byte_offset"]
        del restored[start:start + change["inserted_bytes"]]
    if restored != source:
        raise AssertionError("insertion changed original bytes")
    output = bytes(output)
    return output, changes


def command_counts(ax: bytes) -> dict:
    """Inspect instruction tokens only, without interpreting external tables."""
    if len(ax) < 96 or ax[:4] != b"HSP3":
        raise ValueError("not a complete HSP3 object")
    version, _, total, start, size = struct.unpack_from("<5I", ax, 4)
    if version not in (0x301, 0x360) or start not in (96, 112) or total != len(ax):
        raise ValueError("unsupported AX header/version/size")
    if start + size > len(ax) or size % 2:
        raise ValueError("invalid AX code section")
    counts = Counter()
    position = start
    end = start + size
    total_tokens = 0
    while position < end:
        if position + 4 > end:
            raise ValueError("truncated AX token")
        kind, flags = ax[position:position + 2]
        if flags & 0x0F:
            raise ValueError("unsupported token flags")
        length = 6 if flags & 0x80 else 4
        if kind == 11 and flags & 0x20:
            length += 2
        if position + length > end:
            raise ValueError("truncated AX token value/branch")
        value = struct.unpack_from("<i" if flags & 0x80 else "<H", ax, position + 2)[0]
        if kind == COMMAND_TYPE:
            counts[value] += 1
        total_tokens += 1
        position += length
    return {"object_version": f"0x{version:03x}", "header_bytes": start,
            "total_tokens": total_tokens,
            "type48_commands": {str(k): v for k, v in sorted(counts.items())}}


def check_output_path(path: Path) -> Path:
    # Reject symlink components before resolve() erases evidence of them.
    absolute = path.absolute()
    if any(p.is_symlink() for p in (absolute, *absolute.parents)):
        raise ValueError("output path must not contain symlinks")
    resolved = absolute.resolve()
    work = (ROOT / "work").resolve()
    if not resolved.is_relative_to(work) or resolved == work:
        raise ValueError("output must be a new directory strictly below work/")
    if resolved.exists():
        raise ValueError("output already exists; choose a new directory")
    return resolved


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", type=Path, default=ROOT / "work/decompiled/2.335-reference/start.hsp")
    ap.add_argument("--output", type=Path, required=True, help="new private directory below work/")
    ap.add_argument("--compile", action="store_true", help="compile and verify all managed display command counts")
    ap.add_argument("--compiler", type=Path, default=ROOT / "build/native-macos-arm64-legacy-cp932-release/elona_hspcmp")
    ap.add_argument("--common", type=Path, default=ROOT / ".cache/upstream/OpenHSP/common")
    args = ap.parse_args()
    try:
        destination = check_output_path(args.output)
        original = args.source.read_bytes()
        output, changes = transform(original)
        compiler = args.compiler.resolve()
        common = args.common.resolve()
        if args.compile and (not compiler.is_file() or not common.is_dir()):
            raise ValueError("compiler/common directory missing")
        destination.mkdir(parents=True, exist_ok=False)
        (destination / "start.hsp").write_bytes(output)
        report = {"schema_version": 1, "game_version": "2.335",
                  "source": str(args.source.resolve()), "source_sha256": digest(original),
                  "output_source_sha256": digest(output), "source_encoding": "CP932 bytes preserved",
                  "original_bytes_unchanged": True, "changes": changes,
                  "abi": {"type": COMMAND_TYPE, "commands": {
                      "0": {"name": "gmes", "arguments": 8, "result": "stat < 0: original fallback; otherwise rendered height"},
                      "1": {"name": "begin_english_log", "arguments": ["source", "used", "limit", "starter_pet_id (optional, default 0)"], "result": "preserve stat"},
                      "2": {"name": "hud_draw", "arguments": ["enabled"], "result": "preserve stat"},
                      "6": {"name": "item_label", "arguments": ["label", "original", "alternate", "id"], "special_rows": "id -1: equipment detail; -2 and -3: material name/count and description with original text and material ID", "result": "preserve stat"},
                      "11": {"name": "label_width", "arguments": ["destination"], "result": "localized selection width in pixels or zero"},
                      "10": {"name": "input_caption", "arguments": [], "result": "preserve stat"},
                      "9": {"name": "book", "arguments": ["id", "source", "spread", "x", "y"], "result": "1 if rendered, 0 for original"},
                      "8": {"name": "board", "arguments": ["section", "row", "objective", "ref", "reward", "client", "map", "item"], "result": "preserve stat"},
                      "7": {"name": "menu_draw", "arguments": ["scope", "original"], "result": "preserve stat"}}},
                  "compiled": False,
                  "executed": False}
        success = True
        if args.compile:
            command = [str(compiler), "--compath=" + str(common) + "/",
                       "-o" + str(destination / "start.ax"), str(destination / "start.hsp")]
            with (destination / "compile.log").open("wb") as log:
                process = subprocess.run(command, cwd=destination, stdout=log, stderr=subprocess.STDOUT)
            report.update(compile_command=command, compiler_sha256=digest(compiler.read_bytes()),
                          compile_returncode=process.returncode)
            success = process.returncode == 0 and (destination / "start.ax").is_file()
            if success:
                blob = (destination / "start.ax").read_bytes()
                report.update(compiled=True, ax_sha256=digest(blob), ax_bytes=len(blob))
                try:
                    report["token_check"] = command_counts(blob)
                    hud_commands = 2 * sum(count for _, count in HUD_LINES)
                    menu_commands = output.count(b"\tnative_ko_menu ")
                    success = report["token_check"]["type48_commands"] == {"0": output.count(b"\tnative_ko_gmes "), "1": 2, "2": hud_commands, "6": 6, "7": menu_commands, "8": 1, "9": 1, "10": 3, "11": 3}
                    if not success:
                        report["error"] = "unexpected managed display command counts"
                except ValueError as exc:
                    report["error"] = str(exc)
                    success = False
            else:
                report["error"] = "compile failed; see compile.log"
        report["preparation_passed"] = success
        (destination / "manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"output": str(destination), "prepared": success,
                          "compiled": report["compiled"], "executed": False}, indent=2))
        return 0 if success else 1
    except (OSError, ValueError) as exc:
        print(f"Korean display preparation failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
