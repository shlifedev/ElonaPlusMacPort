#!/usr/bin/env python3
"""Bind Korean drafts to exact, unambiguous 2.335 display keys without publishing source text."""
import argparse
import hashlib
import json
from collections import defaultdict
from pathlib import Path
import re
import unicodedata

ROOT = Path(__file__).resolve().parents[1]
KO = ROOT / 'localization/ko-KR'
RESERVED = re.compile(r'[\x00-\x1f\x7f<>^$@{}%\\\[\]]')
TOKEN = re.compile(r'\d+(?:[.,]\d+)*')
EN_PLACEHOLDER = re.compile(r'untranslated|no translation|^\?+$', re.I)
sha = lambda text: hashlib.sha256(text.encode('utf-8')).hexdigest()


def build(pairs, translations, blocked, english_ids):
    keys = defaultdict(set)
    covered = set()
    by_source = defaultdict(set)
    for row in pairs:
        for source in (row['source_ja'], row['source']):
            by_source[source].add(row['id'])
    for row in pairs:
        ja, en, pid = row['source_ja'], row['source'], row['id']
        if sha(ja + '\0' + en)[:20] != pid:
            raise ValueError('source pair ID mismatch')
        ko = translations.get(pid)
        if not ko:
            continue
        if RESERVED.search(ko) or not re.search('[가-힣]', ko) or len(ko) > 1500:
            continue
        for source in {ja, en}:
            if source == en and source != ja and pid not in english_ids:
                continue
            if not source or not source.strip() or RESERVED.search(source) or len(source.encode()) < 3:
                continue
            if sha(source) in blocked or len(by_source[source]) != 1 or EN_PLACEHOLDER.search(source):
                continue
            normalized = unicodedata.normalize('NFKC', source)
            target_numbers = TOKEN.findall(unicodedata.normalize('NFKC', ko))
            if TOKEN.findall(normalized) != target_numbers:
                continue
            # Avoid aliases of obviously abridged English placeholders. Even
            # seemingly harmless phrases can stand for unrelated Japanese lines.
            if source == en and len(ja.strip()) > 20 and len(en.strip()) < len(ja.strip()) * 0.55:
                continue
            keys[sha(source)].add(ko)
            covered.add(pid)
    return {k: next(iter(v)) for k,v in keys.items() if len(v) == 1}, covered


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pairs', type=Path, default=ROOT/'work/localization/2.335/static-pairs.json')
    ap.add_argument('--translations', type=Path, default=KO/'draft-static.tsv')
    ap.add_argument('--english-ids', type=Path, default=KO/'draft-english-ids.txt',
                    help='Pair IDs whose English actually matches the Japanese source.')
    ap.add_argument('--reviewed-output', type=Path,
                    help='Also emit the subset from the two manually edited pair-ID files.')
    ap.add_argument('--output', type=Path, default=KO/'draft-display.tsv')
    args = ap.parse_args()
    pairs = json.loads(args.pairs.read_text(encoding='utf-8'))
    blocked = set(json.loads((KO/'english-ambiguities.json').read_text(encoding='utf-8'))['key_sha256'])
    translations = {}
    for line in args.translations.read_text(encoding='utf-8').splitlines():
        key, ko = line.split('\t')
        if key in translations or not re.fullmatch('[0-9a-f]{20}', key):
            raise ValueError('invalid or duplicate draft pair ID')
        translations[key] = ko
    if translations.keys() - {row['id'] for row in pairs}:
        raise ValueError('draft has IDs outside the current source')
    english_ids = set(args.english_ids.read_text(encoding='utf-8').splitlines())
    if english_ids - translations.keys():
        raise ValueError('English display bindings include unknown translation IDs')
    keys, covered = build(pairs, translations, blocked, english_ids)
    args.output.write_text(''.join(f'{key}\t{keys[key]}\n' for key in sorted(keys)), encoding='utf-8', newline='\n')
    if args.reviewed_output:
        manual_ids = set()
        for name in ('draft-static-edits.tsv', 'draft-linguistic-edits.tsv'):
            manual_ids.update(line.split('\t')[0] for line in (KO/name).read_text(encoding='utf-8').splitlines())
        edits = {key: value for key, value in translations.items() if key in manual_ids}
        reviewed, edited_pairs = build(pairs, edits, blocked, english_ids)
        if any(keys.get(key) != target for key, target in reviewed.items()):
            raise ValueError('reviewed display bindings must be a subset of the full catalog')
        args.reviewed_output.write_text(''.join(f'{key}\t{reviewed[key]}\n' for key in sorted(reviewed)),
                                        encoding='utf-8', newline='\n')
        print(f'{len(edited_pairs)} manually edited pairs have an additional display binding')
    print(f'{len(covered)} source pairs have a draft binding, {len(keys)} distinct JP/EN display keys')


if __name__ == '__main__':
    main()
