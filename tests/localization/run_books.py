#!/usr/bin/env python3
"""Verify both editions of each translated book in the native reader."""
import argparse
from pathlib import Path
import json
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BOOKS = range(33)


def verify_pixels(path, printed):
    bmp = path.read_bytes()
    offset = struct.unpack_from('<I', bmp, 10)[0]
    width, height, _, bits = struct.unpack_from('<iiHH', bmp, 18)
    assert (width, height, bits) == (636, 340, 24)
    counts = [0, 0]
    for y in range(height):
        row = memoryview(bmp)[offset + (height-1-y)*width*3:][:width*3]
        for x in range(width):
            if row[x*3] < 250:
                column = int(x >= 314)
                assert 8 <= y < 328, (path, x, y)
                assert 8 + 306*column <= x < 314 + 306*column, (path, x, y)
                counts[column] += 1
    assert [n != 0 for n in counts] == printed, (path, counts, printed)
    return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('compiler', 'host', 'work', 'catalog'):
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--common', type=Path, default=ROOT/'.cache/upstream/OpenHSP/common')
    a = ap.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='books-', dir=a.work)).resolve()
    (work/'user').mkdir()
    source = ROOT/'elonaplus2.335/data/book.txt'
    lines = source.read_bytes().decode('cp932').splitlines()
    script = ['#regcmd 48', '#cmd native_ko_book $09', 'screen 0,636,340', 'redraw 0']
    shots = {}
    leaf_count = 0
    for book in BOOKS:
        for lang in ('JP', 'EN'):
            marker = f'%{book},{lang}'
            start = lines.index(marker)
            end = lines.index('%END', start)
            pages = (end - start - 1 + 19)//20
            script += ['notesel body', 'noteload "data\\\\book.txt"', f'p = instr(body, 0, "{book},{lang}")',
                       'body = strmid(body, p, instr(body, p, "%END"))', 'notedel 0']
            leaf_count += pages
            for spread in range((pages+1)//2):
                name = f'{book}-{lang}-{spread}.bmp'
                script += ['color 255,255,255 : boxf',
                           f'native_ko_book {book},body,{spread},8,8', 'if stat != 1 : end 91',
                           f'bmpsave "user\\\\{name}"']
                shots[name] = [
                    any(line.strip() for line in lines[start+1+leaf*20:min(start+1+(leaf+1)*20, end)])
                    for leaf in (spread*2, spread*2+1)
                ]
            script += ['body += "custom"', f'native_ko_book {book},body,0,8,8',
                       'if stat != 0 : end 92']
    script += ['await 1', 'end 0']
    (work/'reader.hsp').write_bytes(('\r\n'.join(script)+'\r\n').encode('cp932'))
    with (work/'compile.log').open('wb') as log:
        subprocess.run([str(a.compiler.resolve()), '--compath='+str(a.common.resolve())+'/',
                        '-o'+str(work/'reader.ax'), str(work/'reader.hsp')], cwd=work,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    with (work/'run.log').open('wb') as log:
        subprocess.run([str(a.host.resolve()), '--game', str(work/'reader.ax'), '--game-root', str(ROOT/'elonaplus2.335'),
                        '--work-dir', str(work), '--user-root', str(work/'user'), '--headless',
                        '--deterministic', '--trace-commands', '--translation', str(a.catalog.resolve()),
                        '--report', str(work/'report.json')], stdout=log, stderr=subprocess.STDOUT, check=True)
    report = json.loads((work/'report.json').read_text())
    assert report['exit_code'] == 0 and not report['first_unsupported'] and not report['observed_hsp_errors']
    for name, leaves in shots.items():
        verify_pixels(work/"user"/"user"/name, leaves)
    print(json.dumps({'leaves': leaf_count, 'spreads': len(shots), 'modified_book_fallbacks': len(BOOKS) * 2,
                      'ink_within_306px_columns': True, 'work': str(work)}))


if __name__ == '__main__':
    main()
