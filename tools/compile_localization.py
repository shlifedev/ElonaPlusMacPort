#!/usr/bin/env python3
"""Compile reviewed Korean JSON entries to strict display-only UTF-8 TSV.

Unreviewed/context_required entries are excluded. Conflicting source mappings
are excluded and reported, never resolved by whichever file happens to be last.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import unicodedata

ROOT = Path(__file__).resolve().parents[1]


def escaped(s):
    return s.replace('\\', '\\\\').replace('\t', '\\t')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('inputs', nargs='+', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--ambiguities', type=Path, default=ROOT / 'localization/ko-KR/english-ambiguities.json')
    args = ap.parse_args()
    try:
        policy = json.loads(args.ambiguities.read_text(encoding='utf-8'))
        if policy['format'] != 'elona-english-ambiguities-v1' or policy['game_version'] != '2.335':
            raise ValueError('unsupported ambiguity policy version')
        hashes = policy['key_sha256']
        if not isinstance(hashes, list) or not hashes or not all(re.fullmatch('[0-9a-f]{64}', h) for h in hashes):
            raise ValueError('expected UTF-8 key SHA-256 hashes')
        ambiguous = set(hashes)
    except (OSError, ValueError, TypeError, KeyError) as error:
        ap.error(f'invalid ambiguity policy {args.ambiguities}: {error}')

    def is_ambiguous(key):
        return hashlib.sha256(key.encode('utf-8')).hexdigest() in ambiguous

    candidates, omitted = defaultdict(set), []
    provenance, input_manifest = defaultdict(list), []
    entries = 0
    for path in args.inputs:
        raw = path.read_bytes()
        data = json.loads(raw.decode('utf-8'))
        try:
            reference = path.resolve().relative_to(ROOT).as_posix()
        except ValueError:
            reference = path.name
        input_manifest.append({'path': reference, 'sha256': hashlib.sha256(raw).hexdigest()})
        data = data['entries'] if isinstance(data, dict) else data
        for row_number, row in enumerate(data):
            entries += 1
            source, target = row.get('source', ''), row.get('target', '')
            if row.get('review_status') != 'reviewed' or not target:
                omitted.append({'source': source, 'reason': row.get('review_status', 'missing review')})
                continue
            target = unicodedata.normalize('NFC', target)
            if any(ord(c) < 32 and c != '\t' for c in target):
                omitted.append({'source': source, 'reason': 'multiline target needs page-aware layout'})
                continue
            keys = []
            if row.get('japanese_enabled') is not False:
                keys.append(row.get('source_ja', ''))
            elif row.get('source_ja'):
                omitted.append({'source': row['source_ja'], 'reason': 'Japanese alias disabled by contextual review'})
            if row.get('english_enabled') is False:
                if source: omitted.append({'source': source, 'reason': 'English alias disabled by contextual review'})
            elif not is_ambiguous(source) or row.get('english_context_reviewed') is True:
                keys.append(source)
            elif source:
                omitted.append({'source': source, 'reason': 'English maps to multiple Japanese meanings'})
            # Aliases are explicit observed display forms (for example a DB
            # name capitalized by the game), never automatic substring/case
            # substitutions. They pass the same ambiguity/conflict checks.
            for alias in row.get('source_aliases', []):
                if not isinstance(alias, str) or not alias:
                    raise ValueError(f'{path}: source_aliases must contain nonempty strings')
                if is_ambiguous(alias) and not row.get('english_context_reviewed'):
                    omitted.append({'source': alias, 'reason': 'display alias has multiple Japanese meanings'})
                else:
                    keys.append(alias)
            for key in filter(None, keys):
                if any(ord(c) < 32 and c != '\t' for c in key):
                    omitted.append({'source': key, 'reason': 'multiline key needs page-aware layout'})
                    continue
                # Retain inline format/control tags exactly; runtime does not
                # invent replacements for parameters or remove color markers.
                tags = lambda x: re.findall(r'<(?:emp\d+|def|title1|p|br|b|green|red|col)>|%[0-9.]*[sdif]', x)
                if tags(key) != tags(target):
                    omitted.append({'source': key, 'reason': 'format/control marker mismatch'})
                    continue
                candidates[key].add(target)
                record = {'path': reference, 'entry_index': row_number}
                if row.get('id'): record['entry_id'] = row['id']
                if row.get('origin', {}).get('license'): record['license'] = row['origin']['license']
                if record not in provenance[key]: provenance[key].append(record)
    conflicts = {s: sorted(v) for s, v in candidates.items() if len(v) != 1}
    output = args.output.resolve()
    original = (ROOT / 'elonaplus2.335').resolve()
    if output == original or original in output.parents:
        ap.error('output cannot be inside original assets')
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('w', encoding='utf-8', newline='\n') as out:
        out.write('# elona-localization-v1: UTF-8 source<TAB>translation; generated from reviewed entries\n')
        for source, targets in sorted(candidates.items()):
            if source not in conflicts:
                out.write(escaped(source) + '\t' + escaped(next(iter(targets))) + '\n')
    report = {'input_entries': entries, 'runtime_keys': len(candidates) - len(conflicts),
              'conflicts': conflicts, 'omitted': omitted,
              'coverage': 'Catalog keys include exact UI strings and context-specific labels. Whole-game coverage is unknown.'}
    output.with_suffix('.report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')
    output.with_suffix('.sources.json').write_text(json.dumps({
        'format': 'elona-localization-provenance-v1', 'inputs': input_manifest,
        'attribution': 'See THIRD_PARTY_NOTICES.md and each referenced JSON origin/review_reason.',
        'keys': {key: provenance[key] for key in sorted(candidates) if key not in conflicts},
    }, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({k: v for k, v in report.items() if k not in ('omitted', 'conflicts')}, ensure_ascii=False))


if __name__ == '__main__':
    main()
