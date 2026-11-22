#!/usr/bin/env python3
"""Compute local LaBSE cosine similarities for Japanese/Korean display candidates."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def pair_hash(ja, ko):
    return hashlib.sha256((ja + '\0' + ko).encode('utf-8')).hexdigest()


def main():
    import torch
    from safetensors.torch import load_file
    from transformers import AutoModel, AutoTokenizer

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--input', type=Path)
    ap.add_argument('--english', action='store_true', help='Score the JP/EN static worklist instead')
    ap.add_argument('--scores', type=Path)
    ap.add_argument('--device', choices=['mps', 'cpu'])
    ap.add_argument('--model', type=Path, default=ROOT / '.cache/ko-model/labse')
    args = ap.parse_args()
    if args.english:
        source = args.input or ROOT / 'work/localization/2.335/static-pairs.json'
        rows = json.loads(source.read_text(encoding='utf-8'))
        raw = [(row['source_ja'], row['source']) for row in rows if row['source_ja'] and row['source']]
        args.scores = args.scores or ROOT / 'work/ko-broad/jp-en-scores.json'
    else:
        source = args.input or ROOT / 'work/ko-broad/translategemma-12b.jsonl'
        raw = [json.loads(line) for line in source.read_text(encoding='utf-8').splitlines()]
        args.scores = args.scores or ROOT / 'work/ko-broad/labse.json'
    scores = json.loads(args.scores.read_text()) if args.scores.exists() else {}
    pending = sorted(((ja, ko) for ja, ko in raw if ko and pair_hash(ja, ko) not in scores),
                     key=lambda pair: max(map(len, pair)))
    if not pending:
        return
    device = args.device or ('mps' if torch.backends.mps.is_available() else 'cpu')
    tokenizer = AutoTokenizer.from_pretrained(args.model, local_files_only=True)
    model = AutoModel.from_pretrained(args.model, local_files_only=True).eval().to(device)
    weights = load_file(str(args.model / '2_Dense/model.safetensors'))
    dense = torch.nn.Linear(768, 768)
    dense.load_state_dict({key.removeprefix('linear.'): value for key, value in weights.items()})
    dense.eval().to(device)

    def encode(texts):
        x = tokenizer(texts, return_tensors='pt', padding=True, truncation=True, max_length=257)
        within_limit = x.attention_mask.sum(dim=1) <= 256
        inputs = {key: value[:, :256].to(device) for key, value in x.items()}
        cls_vectors = model(**inputs).last_hidden_state[:, 0, :]
        embeddings = torch.nn.functional.normalize(torch.tanh(dense(cls_vectors)))
        return embeddings, within_limit.to(device)

    with torch.inference_mode():
        for start in range(0, len(pending), 64):
            batch = pending[start:start + 64]
            jp, ko = zip(*batch)
            source_vectors, source_fits = encode(list(jp))
            target_vectors, target_fits = encode(list(ko))
            cosine = (source_vectors * target_vectors).sum(dim=-1)
            # A shared prefix cannot certify the unseen tail of a longer text.
            cosine = cosine.masked_fill(~(source_fits & target_fits), 0)
            scores.update({pair_hash(*pair): round(value, 6)
                           for pair, value in zip(batch, cosine.tolist(), strict=True)})
    args.scores.write_text(json.dumps(scores, sort_keys=True), encoding='utf-8')
    print(len(scores), 'source/target pairs scored')


if __name__ == '__main__':
    main()
