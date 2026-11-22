#!/usr/bin/env python3
"""Check each reviewed card against the 2.335 database and draw at the real panel width."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser(description=__doc__)
for key in ('work', 'host', 'compiler', 'catalog'):
    ap.add_argument('--' + key, required=True, type=Path)
ap.add_argument('--source', type=Path, default=ROOT / 'work/decompiled/2.335-reference/start.hsp')
a = ap.parse_args()
raw = a.source.read_bytes()
assert hashlib.sha256(raw).hexdigest() == 'c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55'
original = raw.decode('cp932').splitlines()
entries = json.loads((ROOT / 'src/localization/ko-KR/original-card-game-extra.json').read_text())['entries']
headers = {r['card_id']: r for r in entries if r['display_kind'] == 'card heading'}
bodies = {r['card_id']: r for r in entries if r['display_kind'] in ('card biography', 'card detail')}
assert headers.keys() == bodies.keys()
a.work.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix='card-panel-', dir=a.work)).resolve()
hsp = ['#regcmd 48', '#cmd native_ko_gmes $00', 'screen 0,800,600']
for card_id, body in bodies.items():
    if body['display_kind'] == 'card biography':
        line = int(body['source_ref'][0].split(':')[-1])
        assert original[line - 3].strip() == f'if ( dbid == {card_id} ) {{'
        assert original[line - 1].strip().startswith('cardrefskill = lang("' + body['source'] + '",')
    h = headers[card_id]
    start = next(i for i in range(255980, 271240) if original[i].strip() == f'if ( dbid == {card_id} ) {{')
    metadata = '\n'.join(original[start:start + 12])
    rarity = int(re.search(r'cardrefrare = (\d+)', metadata)[1])
    rank = int(re.search(r'cardrefcost = (\d+)', metadata)[1])
    stars = '★' * min(5, max(1, 5 - rarity // 20))
    assert h['target'].endswith(f'랭크 {rank}  희귀도 {stars}'), card_id
    if body['display_kind'] == 'card biography':
        number = re.search(r'cardrefrace = "(\d+) +effect:"', metadata)[1]
        assert 'cardrefattack = 690\n' in metadata and f'번호 {number}  효과: 아무것도 하지 않음' in h['target']
    # Both compact and regular viewports reserve at most two lines below the header.
    for kind, y, max_height in [('header', 542, 18), ('lore', 558, 34)]:
        hsp += [f'native_ko_gmes "@card/{kind}/{card_id}",125,{y},650,0,215,215,215',
                f'if stat < 0 | stat > {max_height} : end {card_id + 1000}']
hsp.append('end 0')
source, ax = work / 'cards.hsp', work / 'cards.ax'
source.write_bytes(('\r\n'.join(hsp) + '\r\n').encode('cp932'))
subprocess.run([a.compiler.resolve(), '-d', '--compath=' + str(ROOT / '.cache/upstream/OpenHSP/common') + '/',
                '-o' + str(ax), source], check=True, stdout=subprocess.PIPE)
for directory in ('game', 'user'):
    (work / directory).mkdir()
report = work / 'report.json'
run = subprocess.run([a.host.resolve(), '--game', ax, '--game-root', work / 'game', '--user-root', work / 'user',
                      '--work-dir', work, '--headless', '--deterministic', '--translation', a.catalog.resolve(),
                      '--trace-commands', '--report', report], capture_output=True)
status = json.loads(report.read_text())
assert run.returncode == 0 and status['endcode'] == 0 and not status['first_unsupported'] and not status['hsp_error'], report
assert status['command_counts']['native_ko_gmes'] == 2 * len(headers)
print(f'{len(headers)} paired card headers and descriptions fit the original tooltip: {work}')
