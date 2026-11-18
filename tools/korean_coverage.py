#!/usr/bin/env python3
"""Count fixed 2.335 source pairs backed by a shipped Korean display entry."""
import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KO = ROOT/'localization/ko-KR'


def digest(s):
    return hashlib.sha256(s.encode('utf-8')).hexdigest()


def measure(pairs, runtime_manifest, draft_display, edited_display, ambiguity_policy):
    reviewed_keys = set(runtime_manifest['keys'])
    counts = Counter(row['source'] for row in pairs)
    categories = Counter()
    for row in pairs:
        ja, en = row['source_ja'], row['source']
        # Repeated English placeholders and names cannot identify one original.
        english_ok = bool(en.strip()) and counts[en] == 1 and digest(en) not in ambiguity_policy
        if ja in reviewed_keys or (english_ok and en in reviewed_keys):
            categories['reviewed_catalog'] += 1
        elif digest(ja) in edited_display or (english_ok and digest(en) in edited_display):
            categories['additional_manually_edited'] += 1
        elif digest(ja) in draft_display or (english_ok and digest(en) in draft_display):
            categories['additional_via_draft'] += 1
        else:
            categories['unbound'] += 1
    return {'version': '2.335', 'source_pairs': len(pairs),
            'reviewed': categories['reviewed_catalog']+categories['additional_manually_edited'],
            'reviewed_catalog': categories['reviewed_catalog'], 'additional_manually_edited': categories['additional_manually_edited'],
            'additional_via_draft': categories['additional_via_draft'], 'unbound': categories['unbound'],
            'total_bound': len(pairs)-categories['unbound'], 'reviewed_display_keys': len(reviewed_keys),
            'draft_display_hashes': len(draft_display), 'manual_display_hashes': len(edited_display),
            'scope': 'Distinct literal lang(ja,en) pairs with a Korean key in at least one language. '
                     'Dynamic expressions, external data and images are outside this denominator. '
                     'Some keys can only be used at a confirmed prose call site.'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pairs', type=Path, default=ROOT/'work/localization/2.335/static-pairs.json')
    p.add_argument('--runtime-manifest', type=Path, default=KO/'runtime.sources.json')
    p.add_argument('--draft-display', type=Path, default=KO/'draft-display.tsv')
    p.add_argument('--reviewed-display', type=Path, default=KO/'reviewed-display.tsv')
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    draft_bytes = a.draft_display.read_bytes()
    edited_bytes = a.reviewed_display.read_bytes()
    runtime_bytes = a.runtime_manifest.read_bytes()
    pair_bytes = a.pairs.read_bytes()
    result = measure(json.loads(pair_bytes), json.loads(runtime_bytes),
                     {row.split('\t')[0] for row in draft_bytes.decode('utf-8').splitlines()},
                     {row.split('\t')[0] for row in edited_bytes.decode('utf-8').splitlines()},
                     set(json.loads((KO/'english-ambiguities.json').read_text(encoding='utf-8'))['key_sha256']))
    result['sha256'] = {name: hashlib.sha256(data).hexdigest() for name, data in
                        [('static_pairs',pair_bytes), ('runtime_sources',runtime_bytes), ('draft_display',draft_bytes), ('reviewed_display',edited_bytes)]}
    serialized = json.dumps(result, indent=2, ensure_ascii=False)+'\n'
    if a.output:
        a.output.write_text(serialized, encoding='utf-8')
    print(serialized, end='')


if __name__ == '__main__':
    main()
