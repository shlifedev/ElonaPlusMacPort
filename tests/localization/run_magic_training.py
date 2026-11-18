#!/usr/bin/env python3
"""Compare actual log and ability-cost pixels to the expected Korean display."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser(description=__doc__)
for name in ('host', 'compiler', 'catalog', 'work'):
    ap.add_argument('--' + name, type=Path, required=True)
a = ap.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(dir=a.work, prefix='log-render-')).resolve()
resource = ROOT / 'localization/ko-KR/original-magic-combat-extra.json'
rows = json.loads(resource.read_text(encoding='utf-8'))['entries']
cases = []
for row in rows:
    if row['source'].startswith('@log/'):
        for key in [row['source'], *row.get('source_aliases', [])]:
            source = key.removeprefix('@log/')
            cases.append(('en' if source.isascii() else 'ja', source, row['target']))
    if row['source'].startswith('@log-noun/{skill}/'):
        for key in [row['source'], *row['source_aliases']]:
            skill = key.split('/')[-1]
            if skill.isascii():
                cases.extend([('en', f'You begin to study {skill}.', row['target'] + ' 공부를 시작했다.'),
                              ('en', f'You finish studying {skill}.', row['target'] + ' 공부를 마쳤다.')])
            else:
                cases.extend([('ja', skill + 'の学習を始めた。', row['target'] + ' 공부를 시작했다.'),
                              ('ja', skill + 'の学習を終えた。', row['target'] + ' 공부를 마쳤다.')])
cases += [('cost', '8 Sp', '8 기력'), ('cost', '20 Sp', '20 기력'),
          ('ja', '[1:24] トレーニングを終えた。', '[1:24] 훈련을 마쳤다.')]
catalog = work / 'test.tsv'
catalog.write_bytes(a.catalog.read_bytes() + b'\n' +
                    ''.join(f'@expected/{i}\t{expected}\n' for i, (_, _, expected) in enumerate(cases)).encode())
images = []
for mode in ('actual', 'expected'):
    folder = work / mode
    folder.mkdir()
    hsp = ['#regcmd 48', '#cmd native_ko_log $01', '#cmd native_ko_menu $07',
           'goto *main', '#deffunc msg_write str chunk', 'mes chunk', 'return', '*main',
           'screen 0,1100,640', 'font "MS Gothic",12']
    for i, (kind, source, _) in enumerate(cases):
        y = (i % 32) * 20
        if y == 0:
            hsp += ['color 255,255,255 : boxf 0,0,1099,639', 'color 0,0,0']
        quote = lambda value: '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'
        if mode == 'expected':
            hsp += [f'pos 8,{y} : mes "@expected/{i}"']
        elif kind == 'en':
            hsp += ['native_ko_log ' + quote(source) + ',0,220,0']
            for word in source.split(' '):
                hsp += [f'pos 8,{y} : msg_write ' + quote(word + ' ')]
        elif kind == 'ja':
            hsp += [f'pos 8,{y} : msg_write ' + quote(source)]
        else:
            hsp += ['native_ko_menu 8,' + quote(source), f'pos 8,{y} : mes ' + quote(source),
                    'native_ko_menu 0,""']
        if i % 32 == 31 or i == len(cases) - 1:
            hsp.append(f'bmpsave "user/page{i//32}.bmp"')
    hsp += ['end 0']
    src = folder / 'fixture.hsp'
    src.write_bytes(('\r\n'.join(hsp) + '\r\n').encode('cp932'))
    ax = folder / 'fixture.ax'
    subprocess.run([a.compiler.resolve(), '-d', '--compath=' + str(ROOT / '.cache/upstream/OpenHSP/common') + '/',
                    '-o' + str(ax), src], check=True, stdout=subprocess.PIPE)
    game, user = folder / 'game', folder / 'user'
    game.mkdir()
    (user / 'user').mkdir(parents=True)
    report = folder / 'report.json'
    run = subprocess.run([a.host.resolve(), '--game', ax, '--game-root', game, '--user-root', user,
                          '--work-dir', folder, '--headless', '--deterministic', '--translation', catalog,
                          '--report', report], capture_output=True, timeout=45)
    (folder / 'host.log').write_bytes(run.stdout + run.stderr)
    status = json.loads(report.read_text())
    assert run.returncode == 0 and not status['first_unsupported'] and not status['hsp_error'], report
    images.append(sorted(folder.rglob('page*.bmp'), key=lambda f: int(f.stem[4:])))
assert len(images[0]) == len(images[1]) == (len(cases) + 31) // 32
for actual, expected in zip(*images):
    assert actual.read_bytes() == expected.read_bytes(), f'{actual} differs from {expected}'
print(f'{len(cases)} log and ability-cost samples matched Korean reference pixels: {work}')
