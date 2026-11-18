#!/usr/bin/env python3
"""Check every reachable 2.335 card heading and exercise the original help-panel draws."""
import argparse
from bisect import bisect_left
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from korean_card_hooks import card_inserts, card_lore_table


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for option in ('compiler', 'host', 'work', 'catalog'):
        ap.add_argument('--' + option, type=Path, required=True)
    ap.add_argument('--source', type=Path, default=ROOT / 'work/decompiled/2.335-reference/start.hsp')
    args = ap.parse_args()
    raw = args.source.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == 'c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55'
    lines = raw.decode('cp932').splitlines()
    entries = {}
    for name in ('original-card-game-extra.json', 'original-card-names.json'):
        for row in json.loads((ROOT/'localization/ko-KR'/name).read_text())['entries']:
            if row['source'].startswith('@card/header/'):
                assert row['card_id'] not in entries
                entries[row['card_id']] = row
    seen = set()
    for i in range(lines.index('*label_5197'), lines.index('*label_5198')):
        branch = re.fullmatch(r'\s*if \( dbid == (\d+) \) \{', lines[i])
        if not branch:
            continue
        card = int(branch[1])
        if card in seen:
            continue  # card_ref returns from the first matching DB branch.
        seen.add(card)
        fields = {}
        for j in range(i+1, i+15):
            if lines[j].strip().startswith('return '):
                break
            k, v = lines[j].strip().split(' = ', 1)
            fields[k] = v
            if k == 'cardrefn':
                source_location = f'2.335:start.hsp:{j+1}'
        row = entries[card]
        assert source_location in row['source_ref']
        assert fields['cardrefattack'] == '690' and fields['cardrefhp'] == '3'
        number = re.match(r'"(\d+) +effect:', fields['cardrefrace'])[1]
        stars = '★' * min(5, max(1, 5 - int(fields['cardrefrare']) // 20))
        assert ' '.join(row['target'].split()).endswith(f' 번호 {number} 효과: 아무것도 하지 않음 랭크 {fields["cardrefcost"]} 희귀도 {stars}')
    assert set(entries) == seen == set(range(1165))
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(dir=args.work, prefix='card-names-')).resolve()
    boundaries = [0]
    for line in raw.splitlines(keepends=True):
        boundaries.append(boundaries[-1] + len(line))
    inserts = {bisect_left(boundaries, offset): code.decode('cp932') for offset, code, _ in card_inserts(raw)}
    cases = [(2, 'data:ignored', ''), (909, 'data:', ''), (909, 'data:First line\\nNext line', 'First line\\nNext line'),
             (909, '情報:日本語の本文', '情報:日本語の本文'), (842, 'data:ignored', ''),
             (908, 'data:ignored', ''), (1500, 'data:Unknown', '')]
    (work/'game').mkdir()
    (work/'user/user').mkdir(parents=True)
    script = ['#regcmd 48', '#cmd native_ko_gmes $00', 'screen 0,800,600',
              'repeat 1165', 'native_ko_gmes "@card/header/"+cnt,120,537,650,0,215,215,215',
              'if stat <= 0 | stat > 18 : end cnt + 100', 'loop', inserts[272403]]
    keys = card_lore_table(raw)
    for index, (card, message, rest) in enumerate(cases):
        for mode in ('actual', 'expected'):
            y = 542 if index % 2 else 537
            x = 125 if index % 2 else 120
            anchor = 273035 if index % 2 else 273040
            script += ['color 0,0,0 : boxf', 'color 215,215,215', f'native_card_help = {card+1}',
                       f'helpmsg@tcg = "original header\\n{message}"', f'pos {x},{y}']
            if mode == 'actual':
                script += [inserts[anchor-1], lines[anchor-1], inserts[anchor]]
            elif card == 1500:
                script += ['mes helpmsg@tcg']
            else:
                script += [f'native_ko_gmes "@card/header/{card}",{x},536,650,0,215,215,215']
                if card == 2:
                    script += [f'native_ko_gmes "@card/lore/{card}",{x},550,650,0,215,215,215']
                elif card in (842, 908):
                    script.append(f'native_ko_gmes {keys[card]},{x},550,650,0,215,215,215')
                elif rest:
                    script += [f'pos {x},{y+16} : mes "{rest}"']
            script.append(f'bmpsave "user/{mode}{index}.bmp"')
    script.append('end 0')
    fixture, ax = work/'fixture.hsp', work/'fixture.ax'
    fixture.write_bytes(('\r\n'.join(script)+'\r\n').encode('cp932'))
    with (work/'compiler.log').open('wb') as log:
        subprocess.run([args.compiler.resolve(), '-d', '--compath='+str(ROOT/'.cache/upstream/OpenHSP/common')+'/',
                        '-o'+str(ax), fixture], check=True, stdout=log)
    report = work/'report.json'
    with (work/'host.log').open('wb') as log:
        subprocess.run([args.host.resolve(), '--game', ax, '--game-root', work/'game', '--user-root', work/'user',
                        '--work-dir', work, '--translation', args.catalog.resolve(), '--headless', '--deterministic',
                        '--report', report], check=True, stdout=log, stderr=subprocess.STDOUT, timeout=60)
    record = json.loads(report.read_text())
    assert record['endcode'] == 0 and not record['first_unsupported'] and not record['hsp_error'], report
    for i in range(len(cases)):
        actual = next(work.rglob(f'actual{i}.bmp'))
        assert actual.read_bytes() == actual.with_name(f'expected{i}.bmp').read_bytes(), actual
    # Every authored biography resolves in the default launcher. In strict
    # mode count only the individually approved or independently corrected rows.
    for mode, fallback in [('default', 'draft-display.tsv'), ('reviewed', 'reviewed-display.tsv')]:
        verify = ['#regcmd 48', '#cmd native_ko_gmes $00', 'screen 0,800,600', inserts[272403]]
        for card in sorted(seen):
            verify += [f'native_ko_gmes "@card/lore/{card}",120,550,650,0,215,215,215',
                       f'if stat < 0 : native_ko_gmes native_card_ja({card}),120,550,650,0,215,215,215',
                       f'if stat > 50 : end {card+1000}', 'if stat > 0 : matched++']
        if mode == 'default':
            verify.append('if matched != 1165 : end 2')
        else:
            verify.append('if matched != 292 : end 3')
        verify += ['notesel result', 'result = str(matched)', f'notesave "user/{mode}.txt"', 'end 0']
        fixture.write_bytes(('\r\n'.join(verify)+'\r\n').encode('cp932'))
        with (work/f'{mode}-compile.log').open('wb') as log:
            subprocess.run([args.compiler.resolve(), '-d', '--compath='+str(ROOT/'.cache/upstream/OpenHSP/common')+'/',
                            '-o'+str(ax), fixture], check=True, stdout=log)
        with (work/f'{mode}-host.log').open('wb') as log:
            subprocess.run([args.host.resolve(), '--game', ax, '--game-root', work/'game', '--user-root', work/'user',
                            '--work-dir', work, '--translation', args.catalog.resolve(), '--translation-fallback',
                            ROOT/'localization/ko-KR'/fallback, '--headless', '--deterministic',
                            '--report', work/f'{mode}.json'], check=True, stdout=log, stderr=subprocess.STDOUT)
        matched = next(work.rglob(f'{mode}.txt')).read_text().strip()
        print(f'{mode}: {matched} complete Korean biographies')
    print(f'{len(entries)} real card IDs fit in one line and {len(cases)} help panels match: {work}')


if __name__ == '__main__':
    main()
