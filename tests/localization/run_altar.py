#!/usr/bin/env python3
"""Compare the two original 2.335 altar menus with Korean reference frames."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
from korean_altar_hooks import install_altar_hooks

ap = argparse.ArgumentParser(description=__doc__)
for option in ('compiler', 'host', 'work'):
    ap.add_argument('--'+option, type=Path, required=True)
ap.add_argument('--catalog', type=Path, help='Test the complete compiled catalog if provided')
a = ap.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(dir=a.work, prefix='altar-')).resolve()
source = (ROOT/'work/decompiled/2.335-reference/start.hsp').read_bytes()
assert hashlib.sha256(source).hexdigest() == 'c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55'
lines = source.decode('cp932').splitlines()
rows = json.loads((ROOT/'src/localization/ko-KR/original-altar-display.json').read_text())['entries']
by_line = {}
entries = {}
for row in rows:
    for key in ('source', 'source_ja'):
        if key in row:
            entries[row[key]] = row['target']
    line = int(row['source_ref'][0].split(':')[-1])
    by_line.setdefault(line, []).append(row)
catalog = work/'fixture.tsv'
lookup = {k:v for k,v in entries.items()}
if a.catalog:
    lookup = dict(line.split('\t', 1) for line in a.catalog.read_text().splitlines() if '\t' in line)
for row in rows:
    assert lookup.get(row['source']) == row['target'], row
    lookup['@ref/'+row['source']] = row['target']
catalog.write_text(''.join(f'{k}\t{v}\n' for k,v in lookup.items()))
hooks = {}


def at(anchor, text, kind, *, after=False):
    assert source.count(anchor) == 1 and after
    hooks[kind] = text.decode().splitlines()


install_altar_hooks(at)
for en in range(2):
    for mode in ('actual', 'expected'):
        folder = work/f'{mode}-{en}'
        (folder/'game').mkdir(parents=True)
        (folder/'user/user').mkdir(parents=True)
        hsp = [lines[10], lines[11], lines[13], lines[14], '#regcmd 48', '#cmd native_ko_gmes $00', '#cmd native_ko_menu $07', 'goto *main', *lines[214:221],
               *lines[7929:7946], '#defcfunc ibit int unused1, int unused2', 'end 5', 'return 0',
               *lines[32244:32291], *lines[275650:275662], *lines[275836:275890],
               '#deffunc gmes str original', 'end 9', 'return', '*main', 'screen 0,650,270',
               'wx=0 : wy=0 : dx=650 : dy=270 : ci=0 : cs=-1 : dim inv,26,1',
               f'en={en} : jp={1-en}', 'gosub *label_5439', 'repeat 9', 'id=cnt',
               'inv(25,ci)=id', 'color 245,238,218 : boxf', 'font "MS Gothic",18-en*2,1',
               'color 0,0,0', 'pos 20,20']
        if mode == 'actual':
            for line in range(275974,276031):
                text = lines[line-1].replace('font lang(cfg_font1, cfg_font2)', 'font "MS Gothic"')
                hsp.append(text)
        else:
            for line in range(275974,276031):
                text = lines[line-1].replace('font lang(cfg_font1, cfg_font2)', 'font "MS Gothic"')
                if text.strip().startswith('bmes '):
                    key = by_line[line][0]['source']
                    text = 'bmes "@ref/'+key+'",'+text.rsplit(',',3)[1]+','+','.join(text.rsplit(',',3)[2:])
                hsp.append(text)
        hsp += [*lines[276030:276036], 'original_buff=buff']
        if mode == 'actual':
            hsp.extend(hooks['altar_paragraph'])
            hsp.append(lines[276036])
            hsp.extend(hooks['altar_end'])
        else:
            hsp += ['native_ko_gmes "@ref/@altar/"+str(id),gmesx,gmesy,gmesw,gmestype,30,30,30',
                    'if stat < 0 : end 8', 'gmesy+=stat']
        # Original: window height 270; first of two choices begins at 270-2*20-18.
        hsp += ['if gmesy > 212 : end 7', 'if original_buff != buff : end 6',
                'font "MS Gothic",14-en*2', 'color 0,0,0', 'pos 80,212']
        for i in range(9):
            believe = by_line[275924][i]
            convert = by_line[275916][i-1] if i else by_line[275913][0]
            row = convert if en else believe
            if mode == 'expected':
                value = '"@ref/'+row['source']+'"'
            elif en:
                value = lines[(275916 if i else 275913)-1].split(' = ',1)[1]
            else:
                value = lines[275924-1].split(' = ',1)[1]
            hsp.append(f'if id == {i} : mes '+value)
        if mode == 'actual':
            hsp += ['i=2', 's='+lines[275928-1].split(' = ',1)[1], *hooks['altar_cancel_begin'],
                    'cs_list s,80,232,19', *hooks['altar_cancel_end']]
        else:
            hsp += ['cs_list "@ref/@altar/cancel",80,232,19']
        hsp += ['bmpsave "user/page"+str(id)+".bmp"', 'loop','end 0']
        src, ax = folder/'fixture.hsp', folder/'fixture.ax'
        src.write_bytes(('\r\n'.join(hsp)+'\r\n').encode('cp932'))
        with (folder/'compile.log').open('wb') as log:
            subprocess.run([a.compiler.resolve(),'-d','--compath='+str(ROOT/'.cache/upstream/OpenHSP/common')+'/',
                            '-o'+str(ax),src],check=True,stdout=log)
        report = folder/'report.json'
        with (folder/'host.log').open('wb') as log:
            subprocess.run([a.host.resolve(),'--game',ax,'--game-root',folder/'game','--user-root',folder/'user',
                            '--work-dir',folder,'--headless','--deterministic','--translation',catalog,
                            '--report',report],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
        status = json.loads(report.read_text())
        assert status['endcode']==0 and not status['first_unsupported'] and not status['hsp_error'], report
    for i in range(9):
        actual = next((work/f'actual-{en}').rglob(f'page{i}.bmp'))
        expected = next((work/f'expected-{en}').rglob(f'page{i}.bmp'))
        assert actual.read_bytes()==expected.read_bytes(), actual
print(f'18 Korean 2.335 altar frames matched; descriptions end above selection rows: {work}')
