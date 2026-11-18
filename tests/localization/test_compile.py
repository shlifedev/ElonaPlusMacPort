#!/usr/bin/env python3
"""Check that ambiguous/unreviewed/structurally changed text cannot ship silently."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='elona-catalog-') as directory:
    work = Path(directory)
    rows = [
        {'source': 'life', 'source_ja': '生命力', 'target': '생명력',
         'source_aliases': ['Life label'], 'review_status': 'reviewed'},
        {'source': 'Weight', 'source_ja': '体重', 'target': '체중', 'review_status': 'reviewed'},
        {'source': 'Weight', 'source_ja': '重さ', 'target': '무게', 'review_status': 'reviewed'},
        {'source': 'unsafe', 'target': '검수 전', 'review_status': 'context_required'},
        {'source': 'conflict', 'target': '하나', 'review_status': 'reviewed'},
        {'source': 'conflict', 'target': '둘', 'review_status': 'reviewed'},
        {'source': '<emp1>word<def>', 'target': '<def>말<emp1>', 'review_status': 'reviewed'},
        {'source': 'declined alias', 'source_ja': '別名', 'target': '별명',
         'english_enabled': False, 'review_status': 'reviewed'},
        {'source': 'page', 'target': '첫 줄\n둘째 줄', 'review_status': 'reviewed'},
    ]
    (work / 'source.json').write_text(json.dumps(rows, ensure_ascii=False), encoding='utf-8')
    subprocess.run([sys.executable, str(ROOT / 'tools/compile_localization.py'), str(work / 'source.json'),
                    '--output', str(work / 'out.tsv')], check=True)
    text = (work / 'out.tsv').read_text(encoding='utf-8')
    entries = dict(line.split('\t') for line in text.splitlines() if not line.startswith('#'))
    assert entries == {'life': '생명력', 'Life label': '생명력', '生命力': '생명력',
                       '体重': '체중', '重さ': '무게', '別名': '별명'}, entries
    report = json.loads((work / 'out.report.json').read_text(encoding='utf-8'))
    assert report['conflicts'] == {'conflict': ['둘', '하나']}, report
    assert len(report['omitted']) == 6, report
print('PASS: catalog aliases, ambiguity, conflicts, review, marker order, multiline boundaries')

with tempfile.TemporaryDirectory(prefix='elona-policy-') as directory:
    output = Path(directory) / 'runtime.tsv'
    base = ROOT / 'localization/ko-KR'
    manifest = json.loads((base / 'runtime.sources.json').read_text(encoding='utf-8'))
    command = [sys.executable, str(ROOT / 'tools/compile_localization.py'),
               *(str(ROOT / entry['path']) for entry in manifest['inputs']), '--output', str(output)]
    missing = subprocess.run(command + ['--ambiguities', str(output.with_suffix('.missing'))], capture_output=True)
    assert missing.returncode and b'invalid ambiguity policy' in missing.stderr and not output.exists()
    subprocess.run(command, check=True, cwd=directory)
    for suffix in ('.tsv', '.report.json', '.sources.json'):
        assert output.with_suffix(suffix).read_bytes() == (base / 'runtime.tsv').with_suffix(suffix).read_bytes()
