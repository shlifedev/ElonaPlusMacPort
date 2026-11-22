#!/usr/bin/env python3
"""Render original 2.335 NPC choice assignments through its real cs_list command."""
import argparse
from bisect import bisect_left
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from korean_static_dialogue import story_choice_inserts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('host', 'compiler', 'work'):
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--source', type=Path, default=ROOT / 'work/decompiled/2.335-reference/start.hsp')
    args = parser.parse_args()
    source = args.source.read_bytes()
    assert hashlib.sha256(source).hexdigest() == 'c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55'
    lines = source.splitlines(keepends=True)
    ends = [0]
    for line in lines:
        ends.append(ends[-1] + len(line))
    added = defaultdict(str)
    for offset, code, kind in story_choice_inserts(source, lines):
        line = bisect_left(ends, offset)
        added[line, kind] = code.decode('cp932')
    def original(n):
        return lines[n-1].decode('cp932').rstrip('\r\n')

    cases = {373951: ('わかった', '알겠다.'), 421903: ('挑戦する', '도전한다'),
             385404: ('嫌い', '싫어'), 384309: ('はい', '그래'),
             384312: ('いいえ', '아니')}
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(dir=args.work, prefix='story-choices-')).resolve()
    catalog = work / 'fixture.tsv'
    catalog.write_text(''.join(f'{ja}\t{ko}\n@choice/{n}\t{ko}\n' for n, (ja, ko) in cases.items()))
    for variant in ('actual', 'expected'):
        d = work / variant
        (d/'game').mkdir(parents=True)
        (d/'user/user').mkdir(parents=True)
        hsp = [original(i) for i in (11, 12, 14, 15)]
        hsp += ['#regcmd 48', '#cmd native_ko_menu $07', 'goto *main']
        hsp.extend(original(i) for i in range(215, 222))
        # ibit is only called by cs_list mode 1 (inventory). NPC rows use mode 0.
        hsp += ['#defcfunc ibit int bitnum, int item', 'end 8', 'return 0']
        for i in range(32245, 32292):
            line = original(i)
            if variant == 'expected' and line.strip() == 'mes cs_list_prm0':
                line = 'mes expected(cnt)'
            hsp.append(line)
        hsp += ['*main', 'screen 0,640,300', 'buffer 3,640,480', 'gsel 0',
                'font "MS Gothic",16', original(23814), added[23814,'npc_reply_init'],
                'sdim expected,64,500', 'sdim check_raw,64,500']
        def add_choice(n, value='Alright.'):
            if n:
                hsp.extend((original(n), added[n,'npc_reply_identity'],
                            f'expected(listmax) = "@choice/{n}"'))
            else:
                hsp.extend((f'listn(0,listmax) = "{value}"', 'expected(listmax) = listn(0,listmax)'))
            hsp.extend(('check_raw(listmax) = listn(0,listmax)', 'list(0,listmax) = listmax * 3 + 7', 'listmax++'))
        for page, choices in enumerate([[373951,421903,385404,384309,384312,0], [0,384309,0], [0,0]]):
            # Also simulate constructing a list and abandoning it before a draw.
            add_choice(421903)
            hsp.extend((original(430313), added[430313,'npc_reply_reset']))
            for n in choices:
                add_choice(n)
            for selection in (0, len(choices)-1):
                hsp += ['color 220,200,166 : boxf', f'cs = {selection}', 'repeat listmax',
                        'x = 10 : y = 10 + cnt * 28']
                if variant == 'actual':
                    hsp.append(added[430274,'npc_reply_begin'])
                hsp.append(original(430275))
                if variant == 'actual':
                    hsp.append(added[430275,'npc_reply_end'])
                hsp += ['if listn(0,cnt) != check_raw(cnt) : end 6',
                        'if list(0,cnt) != cnt * 3 + 7 : end 7', 'loop',
                        f'bmpsave "user/page{page}-{selection}.bmp"']
            hsp.extend((original(430313), added[430313,'npc_reply_reset']))
        hsp.append('end 0')
        fixture, ax = d/'fixture.hsp', d/'fixture.ax'
        fixture.write_bytes(('\r\n'.join(hsp)+'\r\n').encode('cp932'))
        subprocess.run([args.compiler.resolve(), '-d', '--compath='+str(ROOT/'.cache/upstream/OpenHSP/common')+'/',
                        '-o'+str(ax), fixture], check=True, stdout=(d/'compile.log').open('wb'))
        report = d/'report.json'
        with (d/'host.log').open('wb') as log:
            subprocess.run([args.host.resolve(), '--game', ax, '--game-root', d/'game', '--user-root', d/'user',
                            '--work-dir', d, '--translation', catalog, '--headless', '--deterministic',
                            '--report', report], check=True, stdout=log, stderr=subprocess.STDOUT, timeout=30)
        record = json.loads(report.read_text())
        assert record['endcode'] == 0 and not record['first_unsupported'] and not record['hsp_error'], d
    frames = sorted((work/'actual').rglob('*.bmp'))
    assert len(frames) == 6
    for frame in frames:
        reference = work/'expected'/frame.relative_to(work/'actual')
        assert frame.read_bytes() == reference.read_bytes(), frame
    print(f'6 NPC selection frames matched the Korean references: {work}')


if __name__ == '__main__':
    main()
