"""Verify stock 2.335 EN/JP vocabulary using the production C++ alias renderer."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runner', type=Path, required=True)
parser.add_argument('--catalog', type=Path, default=ROOT / 'localization/ko-KR/runtime.tsv')
parser.add_argument('--data', type=Path, default=ROOT / 'elonaplus2.335/data')
args = parser.parse_args()
ko = ROOT / 'localization/ko-KR'
entries = json.loads((ko/'original-alias-words.json').read_text(encoding='utf-8'))['entries']
published = {entry['source']: entry['target'] for entry in entries}
all_keys = set()
cases = set()
for filename, language in [('ndata-e.csv', 'en'), ('ndata.csv', 'ja')]:
    with (args.data / filename).open(encoding='cp932', newline='') as data:
        rows = list(csv.reader(data))
        nouns = {cell.lower() for row in rows for cell in row[:2] if cell}
        for line, row in enumerate(rows, 1):
            for index, cell in enumerate(row[:14]):
                if not cell:
                    continue
                prefix = '@alias-noun/' if language == 'en' and cell.lower() in nouns else '@alias-word/'
                key = prefix + hashlib.sha256(cell.lower().encode('utf-8')).hexdigest()
                all_keys.add(key)
                if key not in published or not re.fullmatch(r'(?:[가-힣]+(?: [가-힣]+)*|[가-힣 ]*\|[가-힣]+(?: [가-힣]+)*)', published[key]):
                    raise ValueError(f'missing Korean forms for {filename}:{line}:{index + 1}')
                # A word may be selected first in any occupied column. Terminal
                # words come from columns 0/1 or 10/11; random_title uses that
                # column family to choose its Japanese endings.
                if language == 'en':
                    cases.add(cell.capitalize() + ' Moon')
                    if index < 2:
                        cases.update(['The ' + cell, cell.capitalize() + ' of Moon', 'Dangerous ' + cell.capitalize()])
                else:
                    cases.add(cell + '月')
                    if index in (0, 1, 10, 11):
                        cases.update(['月の' + cell, 'ソード・' + cell, 'プリンスのの' + cell])
                    if index in (10, 11):
                        cases.update(['ザ・' + cell, cell + '・オブ・ムーン'])
for base in ('The Prince', 'Sword of Flame', 'Drunken Ram'):
    for prefix in ('The army of ', 'The party of ', 'The house of ', 'Clan '):
        cases.add(prefix + base)
    for suffix in ('Clan', 'Party', 'Band', 'Gangs', 'Gathering', 'House', 'Army'):
        cases.add(base + ' ' + suffix)
if set(published) != all_keys:
    raise ValueError('reviewed vocabulary differs from the complete stock word set')
with tempfile.TemporaryDirectory() as tmp:
    samples = Path(tmp, 'cases.txt')
    samples.write_text('\n'.join(sorted(cases)) + '\n', encoding='utf-8')
    subprocess.run([args.runner.resolve(), args.catalog, samples], check=True)
print(f'{len(all_keys)} distinct 2.335 vocabulary cells checked')
