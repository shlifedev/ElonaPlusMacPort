#!/usr/bin/env python3
"""Static lexical audit of recovered CP932 HSP; not a parser or execution trace."""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
RISK_TOKENS = (
    "varptr dupptr peek wpeek lpeek poke wpoke lpoke memcpy memset memexpand "
    "objinfo ginfo mref hwnd hinstance hdc newobj newcom querycom comres delcom "
    "comevent comevarg axobj callfunc newmod delmod setmod strlen strmid instr "
    "getstr getpath bload bsave run exec randomize rnd mesbox objprm objmode "
    "objstyle button input dialog clrobj objsel sendmsg sysinfo dirinfo").split()
TOKEN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:@[A-Za-z_][A-Za-z0-9_]*)?")
LIBRARY = re.compile(r'^\s*#uselib\s+"([^"\r\n]+)"\s*$', re.I)
FUNCTION = re.compile(r'^\s*#(c?func)\s+(\w+)\s+"([^"\r\n]+)"(?:\s+(.*?))?\s*$', re.I)
DIRECTIVE = re.compile(r"^\s*#([A-Za-z_]\w*)\b", re.I)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def mask_strings_and_comments(source: str) -> str:
    """Preserve line/character positions, blank quoted strings and comments.

    This recognizes emitted double-quoted strings with backslash escapes,
    semicolon and // line comments, and /* */ comments. It does not expand
    preprocessor macros or implement the complete HSP grammar.
    """
    result = []
    state = "code"
    i = 0
    while i < len(source):
        c = source[i]
        following = source[i + 1] if i + 1 < len(source) else ""
        if state == "string":
            if c == "\\" and following:
                result.append(" ")
                result.append("\n" if following == "\n" else " ")
                i += 2
                continue
            if c == '"':
                state = "code"
                result.append('"')
            else:
                result.append("\n" if c == "\n" else " ")
        elif state == "line_comment":
            result.append("\n" if c == "\n" else " ")
            if c == "\n":
                state = "code"
        elif state == "block_comment":
            if c == "*" and following == "/":
                result.extend((" ", " "))
                state = "code"
                i += 2
                continue
            result.append("\n" if c == "\n" else " ")
        elif c == '"':
            state = "string"
            result.append(c)
        elif c == ";" or (c == "/" and following == "/"):
            state = "line_comment"
            result.append(" ")
        elif c == "/" and following == "*":
            state = "block_comment"
            result.extend((" ", " "))
            i += 2
            continue
        else:
            result.append(c)
        i += 1
    if state in ("string", "block_comment"):
        raise ValueError(f"unterminated {state}; lexical audit cannot proceed")
    return "".join(result)


def declarations(source_lines: list[str], inventory: dict) -> list[dict]:
    current_library = None
    result = []
    for line_number, line in enumerate(source_lines, 1):
        library_match = LIBRARY.match(line)
        if library_match:
            current_library = library_match[1]
        function_match = FUNCTION.match(line)
        if function_match:
            if current_library is None:
                raise ValueError(f"function declaration at {line_number} has no #uselib")
            parameters = [p.strip() for p in (function_match[4] or "").split(",") if p.strip()]
            result.append({"library": current_library, "alias": function_match[2],
                           "export": function_match[3], "declaration_kind": function_match[1].lower(),
                           "declaration_line": line_number, "source_parameters": parameters})
    expected = [f for f in inventory["functions"] if f["kind"] == "external"]
    if len(result) != len(expected):
        raise ValueError(f"source has {len(result)} external declarations, AX inventory has {len(expected)}")
    for source, binary in zip(result, expected):
        library = inventory["libraries"][binary["library_index"]]["name"]
        if source["library"].casefold() != library.casefold() or source["export"] != binary["name"]:
            raise ValueError(f"declaration ordering/identity mismatch at source line {source['declaration_line']}")
        source["ax_function_index"] = binary["index"]
    return result


def snippet(line: str) -> str:
    # Include code only; never reproduce dialogue, URLs or game data strings.
    return re.sub(r'" +"', '"<string>"', line.strip())[:220]


def audit(source: bytes, inventory_bytes: bytes) -> dict:
    text = source.decode("cp932")
    lines = text.splitlines()
    inventory = json.loads(inventory_bytes)
    functions = declarations(lines, inventory)
    by_alias = {f["alias"].casefold(): f for f in functions}
    if len(by_alias) != len(functions):
        raise ValueError("duplicate source function aliases require a scoped parser")
    for f in functions:
        f.update({"approximate_call_count": 0, "lexical_mentions": 0, "sample_lines": []})
    masks = mask_strings_and_comments(text).splitlines()
    risks = {name: {"token_mentions": 0, "matching_line_count": 0, "examples": []} for name in RISK_TOKENS}
    directives = Counter()
    arrows = []
    cp932_branches = []
    ginfo_indices = Counter()
    for number, (line, masked) in enumerate(zip(lines, masks), 1):
        directive_match = DIRECTIVE.match(line)
        if directive_match:
            directives[directive_match[1].lower()] += 1
            continue
        matched_risks = set()
        for match in TOKEN.finditer(masked):
            name = match[0].casefold()
            if name in risks:
                risks[name]["token_mentions"] += 1
                matched_risks.add(name)
            if name in by_alias:
                f = by_alias[name]
                f["lexical_mentions"] += 1
                before, after = masked[:match.start()].rstrip(), masked[match.end():].lstrip()
                candidate = (after.startswith("(") if f["declaration_kind"] == "cfunc"
                             else not before or before.endswith((":", "{")))
                if candidate:
                    f["approximate_call_count"] += 1
                    if len(f["sample_lines"]) < 5:
                        f["sample_lines"].append(number)
        for name in matched_risks:
            entry = risks[name]
            entry["matching_line_count"] += 1
            if len(entry["examples"]) < 4:
                entry["examples"].append({"line": number, "code": snippet(masked)})
        if "->" in masked and len(arrows) < 12:
            arrows.append({"line": number, "code": snippet(masked)})
        if all(value in masked for value in ("129", "159", "224", "252")):
            cp932_branches.append({"line": number, "code": snippet(masked)})
        for index in re.findall(r"(?<![\w@])ginfo\s*\(\s*(-?\d+)\s*\)", masked, flags=re.I):
            ginfo_indices[index] += 1
    libraries = []
    for library in inventory["libraries"]:
        members = [f for f in functions if f["library"].casefold() == library["name"].casefold()]
        libraries.append({"name": library["name"], "declared_functions": len(members),
                          "functions_with_lexical_call_sites": sum(f["approximate_call_count"] > 0 for f in members),
                          "approximate_call_count": sum(f["approximate_call_count"] for f in members)})
    return {"schema_version": 1,
            "source": {"sha256": sha256(source), "bytes": len(source), "encoding": "cp932", "lines": len(lines)},
            "ax": {"sha256": inventory["sha256"], "inventory_sha256": sha256(inventory_bytes)},
            "method": "Comment/string masking plus case-insensitive identifier and statement-shape matching; not a complete HSP parser.",
            "limitations": ["Counts are static lexical call-site candidates, not execution frequency or reachability.",
                            "Branch conditions, loops, labels, callbacks and macros are not evaluated.",
                            "Zero matches do not prove that a declaration or feature is safe to remove.",
                            "Source line references apply only to the recorded recovered-source SHA-256.",
                            "String content is redacted from code examples; CP932 bytes are not rewritten."],
            "libraries": libraries, "functions": functions, "risk_constructs": risks,
            "directive_counts": dict(sorted(directives.items())),
            "ginfo_literal_indices": dict(sorted(ginfo_indices.items())),
            "module_parameter_occurrences": sum(p["type"] in (-7, -11, -20)
                                                for f in inventory["functions"] for p in f["parameters"]),
            "cp932_lead_byte_branches": cp932_branches,
            "com_arrow_examples": arrows,
            "totals": {"libraries": len(libraries), "external_declarations": len(functions),
                       "approximate_call_count": sum(f["approximate_call_count"] for f in functions),
                       "declarations_with_zero_lexical_calls": sum(f["approximate_call_count"] == 0 for f in functions)}}


def markdown(report: dict) -> str:
    result = ["# 2.335 static compatibility inventory", "",
              f"Source SHA-256: `{report['source']['sha256']}`", "",
              report["method"], "", "**These are candidate source call sites, not runtime reachability or execution counts.**", "",
              "## Libraries", "", "| Library | Declarations | With candidate calls | Candidate call sites |",
              "|---|---:|---:|---:|"]
    for row in report["libraries"]:
        result.append(f"| {row['name']} | {row['declared_functions']} | {row['functions_with_lexical_call_sites']} | {row['approximate_call_count']} |")
    result += ["", "## Functions", "", "| Library | Alias / export | Decl. line | Candidate calls | Example lines |", "|---|---|---:|---:|---|"]
    for row in report["functions"]:
        examples = ", ".join(str(n) for n in row["sample_lines"])
        result.append(f"| {row['library']} | `{row['alias']}` / `{row['export']}` | {row['declaration_line']} | {row['approximate_call_count']} | {examples} |")
    result += ["", "## High-risk constructs", "", "| Token | Mentions | Matching lines | Example lines |", "|---|---:|---:|---|"]
    for name, row in report["risk_constructs"].items():
        examples = ", ".join(str(e["line"]) for e in row["examples"])
        result.append(f"| `{name}` | {row['token_mentions']} | {row['matching_line_count']} | {examples} |")
    result += ["", "## Limits", ""] + [f"- {item}" for item in report["limitations"]]
    return "\n".join(result) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "work/decompiled/2.335/start.hsp")
    parser.add_argument("--inventory", type=Path, default=ROOT / "work/extracted/2.335/inventory.json")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "work/reports/compatibility-2.335")
    args = parser.parse_args()
    try:
        output = args.output_dir.resolve()
        if ROOT / "work" / "reports" not in output.parents:
            raise ValueError("output must be a directory beneath work/reports")
        report = audit(args.source.read_bytes(), args.inventory.read_bytes())
        outputs = {"audit.json": (json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8"),
                   "audit.md": markdown(report).encode("utf-8")}
        if args.output_dir.is_symlink():
            raise ValueError("output directory must not be a symlink")
        if output.exists() and {p.name for p in output.iterdir()} != set(outputs):
            raise ValueError("existing output has unexpected files; select a new directory")
        for name, content in outputs.items():
            target = output / name
            if target.is_symlink() or (target.exists() and target.read_bytes() != content):
                raise ValueError(f"existing output differs: {target}; select a new directory")
        output.mkdir(parents=True, exist_ok=True)
        for name, content in outputs.items():
            target = output / name
            if not target.exists():
                with target.open("xb") as stream:
                    stream.write(content)
    except (ValueError, OSError, KeyError) as exc:
        print(f"audit_compatibility: {exc}", file=sys.stderr)
        return 1
    print(f"Reports: {output}")
    print(json.dumps(report["totals"], sort_keys=True))
    print("Static lexical candidates only; no game execution or reachability analysis.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
