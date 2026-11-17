#!/usr/bin/env python3
"""Extract a private translation worklist; never edit game source/AX/assets.

Static lang(Japanese, English) pairs are exact only when both arguments are
string literals. Dynamic expressions remain a separate untranslated worklist.
Counts are source coverage, not an assertion about displayed coverage.
"""
import argparse
from bisect import bisect_right
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
LITERAL = r'"(?:\\.|[^"\\])*"'
PAIR = re.compile(r'\blang\(\s*(' + LITERAL + r')\s*,\s*(' + LITERAL + r')\s*\)')


def unquote(token):
    # HSP compiler string escapes; avoid Python unicode_escape corrupting JP.
    escaped = {'n': '\r\n', 't': '\t', '"': '"', '\\': '\\'}
    return re.sub(r'\\(.)', lambda m: escaped[m[1]], token[1:-1])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, default=ROOT / 'work/decompiled/2.335-reference/start.hsp')
    ap.add_argument('--output', type=Path, default=ROOT / 'work/localization/2.335')
    args = ap.parse_args()
    raw = args.source.read_bytes()
    source = raw.decode('cp932')
    line_starts = [0] + [m.end() for m in re.finditer('\n', source)]
    rows = {}
    matched_offsets = set()
    english_to_ja = defaultdict(set)
    for m in PAIR.finditer(source):
        ja, en = unquote(m[1]), unquote(m[2])
        key = hashlib.sha256((ja + '\0' + en).encode()).hexdigest()[:20]
        row = rows.setdefault(key, {'id': key, 'source_ja': ja, 'source': en, 'target': '',
                                    'review_status': 'untranslated', 'source_lines': []})
        row['source_lines'].append(bisect_right(line_starts, m.start()))
        english_to_ja[en].add(ja)
        matched_offsets.add(m.start())
    ambiguous = {en: sorted(ja) for en, ja in english_to_ja.items() if len(ja) > 1}
    dynamic = [{'line': bisect_right(line_starts, m.start()),
                'source_line': source[m.start():source.find('\n', m.start())]}
               for m in re.finditer(r'\blang\(', source) if m.start() not in matched_offsets]
    out = args.output.resolve()
    original = (ROOT / 'elonaplus2.335').resolve()
    if out == original or original in out.parents:
        ap.error('output cannot be inside original assets')
    out.mkdir(parents=True, exist_ok=True)
    def write(name, value):
        (out / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
    write('static-pairs.json', list(rows.values()))
    write('ambiguous-english.json', ambiguous)
    write('dynamic-expressions.json', dynamic)
    external = []
    for filename in ('exhelp.txt',):
        path = ROOT / 'elonaplus2.335/data' / filename
        if path.is_file():
            b = path.read_bytes()
            external.append({'path': str(path.relative_to(ROOT)), 'sha256': hashlib.sha256(b).hexdigest(),
                             'bytes': len(b), 'format': 'CP932 %id,JP/EN blocks; blank pages and color tags significant'})
    write('manifest.json', {'version': '2.335', 'source_sha256': hashlib.sha256(raw).hexdigest(),
                           'static_occurrences': len(matched_offsets), 'unique_static_pairs': len(rows),
                           'dynamic_expressions': len(dynamic), 'ambiguous_english_keys': len(ambiguous),
                           'external_sources': external, 'limitations': ['not all literals are lang() calls',
                           'dynamic expressions require contextual templates; no naive global substitution',
                           'dialogue/page splitting must be validated in the running game']})
    print((out / 'manifest.json').read_text())


if __name__ == '__main__':
    main()
