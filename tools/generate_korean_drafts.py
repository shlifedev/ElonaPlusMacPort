#!/usr/bin/env python3
"""Generate local Japanese-to-Korean static display candidates for the 2.335 catalog."""
import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import re
import unicodedata

from score_korean_drafts import pair_hash

ROOT = Path(__file__).resolve().parents[1]
JAPANESE = re.compile(r'[ぁ-ヺ一-鿿]')
CONTROL = re.compile(r'[\x00-\x1f%#@{}<>\[\]\\|$*]')
HANGUL = re.compile(r'[가-힣]')


def eligible(ja):
    return JAPANESE.search(ja) and not CONTROL.search(ja) and 2 <= len(ja) <= 400


def approved_shape(ja, ko, terms):
    if any(char in ko for char in ('⁇', '�', '□', '▁', '※')):
        return False
    if ja.startswith(('は', 'が', 'を', 'に', 'の', 'から', 'へと', 'より')) and ja.endswith(('。', '！')):
        return False
    if any(char.isalpha() and not HANGUL.fullmatch(char) and char not in ja for char in ko):
        return False
    if not HANGUL.search(ko) or CONTROL.search(ko) or JAPANESE.search(ko):
        return False
    if ko.count('"') % 2 or '""' in ko or '””' in ko or '““' in ko:
        return False
    for opening, closing in [('“', '”'), ('‘', '’'), ('「', '」'), ('『', '』'), ('(', ')')]:
        if ko.count(opening) != ko.count(closing):
            return False
    if '「' in ja and '」' in ja and not any(char in ko for char in ('"', '“', '「', '『', '‘')):
        return False
    if any(unicodedata.category(char) == 'Cf' for char in ko):
        return False
    if re.findall(r'[★☆♪♂♀↑↓←→]', ja) != re.findall(r'[★☆♪♂♀↑↓←→]', ko):
        return False
    if re.findall(r'\d+', ja) != re.findall(r'\d+', ko):
        return False
    if re.findall(r'[A-Za-z]+', ja) != re.findall(r'[A-Za-z]+', ko):
        return False
    if not 0.45 * len(ja) <= len(ko) <= 3 * len(ja) + 3:
        return False
    if re.search(r'(.{2,20})\1\1', ko) or re.search(r'(\S+)(?: \1){2}', ko):
        return False
    if any(name in ja and target not in ko for name, target in terms):
        return False
    return True


def translation_prompt(tokenizer, ja):
    message = {'role': 'user', 'content': [
        {'type': 'text', 'source_lang_code': 'ja', 'target_lang_code': 'ko', 'text': ja}]}
    return tokenizer.apply_chat_template([message], tokenize=True, add_generation_prompt=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pairs', type=Path, default=ROOT / 'work/localization/2.335/static-pairs.json')
    ap.add_argument('--model', type=Path, default=ROOT / '.cache/ko-model/translategemma')
    ap.add_argument('--cache', type=Path, default=ROOT / 'work/ko-broad/translategemma-12b.jsonl')
    ap.add_argument('--output', type=Path, default=ROOT / 'localization/ko-KR/draft-static.tsv')
    ap.add_argument('--scores', type=Path, default=ROOT / 'work/ko-broad/labse.json')
    ap.add_argument('--english-scores', type=Path, default=ROOT / 'work/ko-broad/jp-en-scores.json')
    ap.add_argument('--english-ids', type=Path, default=ROOT / 'localization/ko-KR/draft-english-ids.txt')
    ap.add_argument('--export-only', action='store_true')
    args = ap.parse_args()
    rows = json.loads(args.pairs.read_text(encoding='utf-8'))
    directory = ROOT / 'localization/ko-KR'
    reviewed = {s.partition('\t')[0] for s in (directory / 'runtime.tsv').read_text(encoding='utf-8').splitlines()}
    with (directory / 'glossary.tsv').open(encoding='utf-8') as f:
        terms = [(t['ja'], t['ko']) for t in csv.DictReader(f, delimiter='\t')
                 if ((t['domain'] in {'place', 'race', 'god', 'lore', 'npc'} and len(t['ja']) >= 3)
                     or t['ja'] in {'生命力', '筋力', '魔力', '混沌', '電撃', '地獄', '暗黒', '幻惑',
                                     '火炎', '冷気', '出血', 'エーテル', '魅力'})]
    candidates = {}
    for row in rows:
        ja, en = row['source_ja'], row['source']
        if hashlib.sha256((ja + '\0' + en).encode()).hexdigest()[:20] != row['id']:
            raise ValueError(f"Invalid static pair ID {row['id']}")
        if ja not in reviewed and eligible(ja) and not CONTROL.search(en):
            candidates[row['id']] = ja
    args.cache.parent.mkdir(parents=True, exist_ok=True)
    stored = dict(json.loads(line) for line in args.cache.read_text().splitlines()) if args.cache.exists() else {}
    if not args.export_only:
        import mlx.core as mx
        from mlx_lm import batch_generate, load
        from mlx_lm.models.cache import make_prompt_cache
        if not args.model.is_dir():
            raise FileNotFoundError(f'Local model directory missing: {args.model}')
        mx.set_cache_limit(2 * 1024**3)
        model, tokenizer = load(str(args.model))
        tokenizer.add_eos_token('<end_of_turn>')
        # Cache only shared instruction tokens; Japanese source and the model
        # turn marker remain separate for every line.
        first = translation_prompt(tokenizer, 'Ａ')
        second = translation_prompt(tokenizer, '日')
        prefix_size = next(i for i, tokens in enumerate(zip(first, second)) if tokens[0] != tokens[1])
        prefix = first[:prefix_size]
        shared = make_prompt_cache(model)
        model(mx.array(prefix)[None, :], cache=shared)
        mx.eval([entry.state for entry in shared])
        pending = sorted(set(candidates.values()) - stored.keys(), key=lambda s: (len(s), s))
        with args.cache.open('a', encoding='utf-8') as f:
            for begin in range(0, len(pending), 64):
                batch = pending[begin:begin + 64]
                prompts = [translation_prompt(tokenizer, ja) for ja in batch]
                if any(prompt[:prefix_size] != prefix for prompt in prompts):
                    raise ValueError('Unexpected TranslateGemma prompt prefix')
                suffixes = [prompt[prefix_size:] for prompt in prompts]
                limits = [min(600, 2 * len(ja) + 30) for ja in batch]
                result = batch_generate(model, tokenizer, suffixes, max_tokens=limits,
                                        prompt_caches=[shared] * len(batch),
                                        completion_batch_size=24, prefill_batch_size=4,
                                        return_token_ids=True)
                for ja, ko, tokens, limit in zip(batch, result.texts, result.token_ids, limits, strict=True):
                    target = ko.strip() if len(tokens) < limit else ''
                    stored[ja] = target
                    print(json.dumps([ja, target], ensure_ascii=False), file=f)
                f.flush()
                print(begin + len(batch), '/', len(pending), flush=True)
        # Selection is a separate pass after semantic scoring.
        return

    scores = json.loads(args.scores.read_text(encoding='utf-8'))
    accepted = {}
    for key, ja in candidates.items():
        ko = stored.get(ja, '')
        score = scores.get(pair_hash(ja, ko), 0)
        # Short terms are often polysemous. A high score still needs editorial review.
        minimum = 0.78 if len(ja) <= 7 else 0.72
        if not minimum <= score <= 1.00001 or not approved_shape(unicodedata.normalize('NFKC', ja),
                                                  unicodedata.normalize('NFKC', ko), terms):
            continue
        # Avoid sentence stops appended to short interface labels and nouns.
        if len(ja) <= 10 and not re.search(r'[。！？?!…]$', ja) and ko.endswith('.'):
            ko = ko[:-1]
        left_padding = ja[:len(ja) - len(ja.lstrip(' '))]
        right_padding = ja[len(ja.rstrip(' ')):]
        accepted[key] = left_padding + ko + right_padding
    sources_by_output = Counter(ko for _, ko in {(candidates[k], ko) for k, ko in accepted.items()})
    accepted = {k: ko for k, ko in accepted.items() if sources_by_output[ko] <= 3}
    all_pairs = {row['id']: row for row in rows}
    edit_files = [directory / 'draft-static-edits.tsv', directory / 'draft-linguistic-edits.tsv']
    for edit_file in edit_files:
        seen_edits = set()
        for line in edit_file.read_text(encoding='utf-8').splitlines():
            key, ko = line.split('\t')
            if key in seen_edits or not ko or key not in all_pairs:
                raise ValueError(f'Invalid manual correction {key} in {edit_file}')
            seen_edits.add(key)
            ja = all_pairs[key]['source_ja']
            source = unicodedata.normalize('NFKC', ja)
            target = unicodedata.normalize('NFKC', ko)
            if re.findall(r'\d+', source) != re.findall(r'\d+', target):
                raise ValueError(f'Number mismatch for manual correction {key}')
            if any(ord(char) < 32 or ord(char) == 127 for char in ko):
                raise ValueError(f'Invalid control character for manual correction {key}')
            # Manual edits may preserve authored formatting but cannot introduce
            # a new printf field, name substitution, or rendering command.
            if any(char in target and char not in source for char in '%#@{}<>\\|$'):
                raise ValueError(f'New rendering control in manual correction {key}')
            if ja not in reviewed:
                accepted[key] = ko
    output = ''.join(f'{k}\t{ko}\n' for k, ko in sorted(accepted.items()))
    args.output.write_text(output, encoding='utf-8', newline='\n')
    english_scores = json.loads(args.english_scores.read_text(encoding='utf-8'))
    en_ids = []
    for key in sorted(accepted):
        row = all_pairs[key]
        if english_scores.get(pair_hash(row['source_ja'], row['source']), 0) >= 0.70:
            en_ids.append(key)
    args.english_ids.write_text(''.join(key + '\n' for key in en_ids), encoding='ascii', newline='\n')
    print(f'{len(en_ids)} English aliases passed the source-equivalence threshold')
    print(f'{len(accepted)} selected from {len(candidates)} candidate pairs', flush=True)


if __name__ == '__main__':
    main()
