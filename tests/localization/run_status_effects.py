#!/usr/bin/env python3
"""Render the original player addbuff messages and compare each row with Korean reference pixels."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
for option in ('host', 'compiler', 'catalog', 'work'):
    parser.add_argument('--' + option, type=Path, required=True)
parser.add_argument('--source', type=Path, default=ROOT / 'work/decompiled/2.335/start.hsp')
args = parser.parse_args()
original = args.source.read_bytes().decode('cp932', errors='replace')
assert 'name(addbuff_prm0) + " " + bufftxt(0, addbuff_prm1) + _s(addbuff_prm0) + bufftxt(1, addbuff_prm1)' in original
buffs = re.findall(r'bufftxt\(0, (\d+)\) = lang\("([^"]+)", "([^"]+)"\), "([^"]*)"', original)
assert len(buffs) == 71 and len({row[0] for row in buffs}) == 71
resource = ROOT / 'src/localization/ko-KR/original-static-status.json'
rows = json.loads(resource.read_text(encoding='utf-8'))['entries']
targets = {row['source'][5:]: row['target'] for row in rows}
cases = {}
for _, japanese, verb, tail in buffs:
    english = 'You ' + verb + tail
    for source in [english, 'you ' + verb + tail, 'あなた' + japanese]:
        cases[source] = targets[english]
args.work.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix='status-', dir=args.work)).resolve()
catalog = work / 'catalog.tsv'
catalog.write_bytes(args.catalog.read_bytes() + b'\n' + ''.join(
    f'@reference/{i}\t{target}\n' for i, target in enumerate(cases.values())).encode())
hsp = ['#regcmd 48', '#cmd native_ko_log $01', 'goto *main', '#deffunc msg_write str chunk',
       'mes chunk', 'return', '*main', 'screen 0,1200,480', 'font "MS Gothic",12']
for i, source in enumerate(cases):
    y = i % 24 * 20
    if y == 0:
        hsp += ['color 255,255,255 : boxf 0,0,1199,479', 'color 0,0,0']
    if source.isascii():
        hsp += [f'native_ko_log "{source}",0,220,0']
        for word in source.split(' '):
            hsp += [f'pos 8,{y} : msg_write "{word} "']
    else:
        hsp += [f'pos 8,{y} : msg_write "{source}"']
    hsp += [f'pos 608,{y} : mes "@reference/{i}"']
    if y == 460 or i + 1 == len(cases):
        hsp += [f'bmpsave "user/page{i//24}.bmp"']
src = work / 'fixture.hsp'
src.write_bytes(('\r\n'.join(hsp + ['end 0']) + '\r\n').encode('cp932'))
ax = work / 'fixture.ax'
subprocess.run([args.compiler.resolve(), '-d', '--compath=' + str(ROOT / '.cache/upstream/OpenHSP/common') + '/',
                '-o' + str(ax), src], check=True, capture_output=True)
(work / 'game').mkdir()
user = work / 'user'
(user / 'user').mkdir(parents=True)
report = work / 'report.json'
subprocess.run([args.host.resolve(), '--game', ax, '--game-root', work / 'game', '--user-root', user,
                '--work-dir', work, '--headless', '--deterministic', '--translation', catalog,
                '--report', report], check=True, capture_output=True, timeout=60)
status = json.loads(report.read_text())
assert not status['first_unsupported'] and not status['hsp_error'], report
for i, source in enumerate(cases):
    if i % 24 == 0:
        bmp = (user / 'user' / f'page{i//24}.bmp').read_bytes()
        offset = struct.unpack_from('<I', bmp, 10)[0]
        width, height = struct.unpack_from('<ii', bmp, 18)
        bits = struct.unpack_from('<H', bmp, 28)[0]
        assert (width, abs(height)) == (1200, 480) and bits in (24, 32)
        half = 600 * (bits // 8)
    block = bytearray()
    for y in range(i % 24 * 20, i % 24 * 20 + 20):
        start = offset + (479-y if height > 0 else y) * half * 2
        actual = bmp[start:start+half]
        assert actual == bmp[start+half:start+half*2], (source, work)
        block.extend(actual)
    assert len(set(block)) > 2, (source, 'empty reference row')
print(f'{len(buffs)} source effect IDs, {len(cases)} Korean reference rows matched: {work}')
