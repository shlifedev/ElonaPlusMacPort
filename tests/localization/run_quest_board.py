#!/usr/bin/env python3
"""Render all 2.335 board.txt postings with the native board ABI in a fresh fixture."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def quote(value):
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('compiler', 'host', 'catalog', 'work'):
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--common', type=Path, default=ROOT / '.cache/upstream/OpenHSP/common')
    a = ap.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='board-', dir=a.work)).resolve()
    table = dict(l.split('\t', 1) for l in a.catalog.read_text(encoding='utf-8').splitlines()
                 if l and not l.startswith('#'))
    hsp = ['#regcmd 48', '#cmd native_ko_gmes $00', '#cmd native_ko_board $08',
           'screen 0,560,140', 'redraw 0']
    counts = Counter()
    section = ''
    for line in (ROOT / 'elonaplus2.335/data/board.txt').read_bytes().decode('cp932').splitlines():
        if line.startswith('%'):
            section = line
        elif section and section != '%END' and ':' in line:
            lang = section.rsplit(',', 1)[1]
            counts[lang] += 1
            key = '@board/' + section + '/' + hashlib.sha256(line.encode()).hexdigest()
            assert table[key + '/title']
            pattern = table[key + '/body']
            fields = r'\{(?:ref|objective|client|reward|map)\}'
            assert Counter(re.findall(fields, pattern)) == Counter(re.findall(fields, line)), key
            # A name may resemble a rich text tag, a template, or another lookup key.
            arguments = [section, line, 'a rifle', '5 points', '235 gold pieces', '<b>{reward}$^', 'Vernis']
            hsp += ['native_ko_board ' + ', '.join(map(quote, arguments)) + ', 1',
                    'native_ko_gmes "@board/body", 20, 20, 520, 0, 0, 0, 0',
                    'if stat < 0 : end 90']
    if counts != {'EN': 25, 'JP': 63}:
        raise ValueError(f'unexpected source row count: {counts}')
    hsp += ['native_ko_board "%HUNT,EN","Hunting.:custom body","","","","","",-1',
            'native_ko_gmes "@board/body",20,20,520,0,0,0,0', 'if stat != -1 : end 91',
            'await 1', 'end 0']
    (work/'board.hsp').write_bytes(('\r\n'.join(hsp)+'\r\n').encode('cp932'))
    with (work/'compile.log').open('wb') as log:
        subprocess.run([str(a.compiler.resolve()), '--compath='+str(a.common.resolve())+'/',
                        '-o'+str(work/'board.ax'), str(work/'board.hsp')],
                       cwd=work, stdout=log, stderr=subprocess.STDOUT, check=True)
    (work/'user').mkdir()
    with (work/'run.log').open('wb') as log:
        subprocess.run([str(a.host.resolve()), '--game', str(work/'board.ax'),
                        '--game-root', str(ROOT/'elonaplus2.335'), '--user-root', str(work/'user'),
                        '--packed-dir', str(ROOT/'work/extracted/2.335'),
                        '--work-dir', str(work), '--headless', '--deterministic', '--trace-commands',
                        '--translation', str(a.catalog.resolve()), '--report', str(work/'report.json')],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    report = json.loads((work/'report.json').read_text())
    assert report['command_counts']['native_ko_board'] == sum(counts.values()) + 1
    assert report['exit_code'] == 0 and not report['first_unsupported'] and not report['observed_hsp_errors']
    print(json.dumps({'rendered': counts, 'unknown_row_fallback': True, 'work': str(work)}))


if __name__ == '__main__':
    main()
