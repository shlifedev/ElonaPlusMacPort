#!/usr/bin/env python3
"""Render both languages of the fixed log choices using the shipping native log boundary."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def quoted(value):
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def render(folder, host, compiler, catalog, pages):
    folder.mkdir()
    lines = ['#regcmd 48', '#cmd native_ko_log $01', 'goto *main',
             '#deffunc msg_write str chunk', 'mes chunk', 'return', '*main',
             'screen 0,1200,640', 'font "MS Gothic",12']
    for page, drawing in enumerate(pages):
        lines += ['color 255,255,255 : boxf', 'color 0,0,0', *drawing, f'bmpsave "user/page{page}.bmp"']
    path = folder / 'render.hsp'
    path.write_bytes(('\r\n'.join(lines + ['end 0', ''])).encode('cp932'))
    ax = folder / 'render.ax'
    subprocess.run([compiler, '--compath=' + str(ROOT / '.cache/upstream/OpenHSP/common') + '/',
                    '-o' + str(ax), path], cwd=folder, check=True, stdout=subprocess.PIPE)
    (folder / 'game').mkdir()
    (folder / 'user/user').mkdir(parents=True)
    report = folder / 'report.json'
    subprocess.run([host, '--game', ax, '--game-root', folder / 'game', '--user-root', folder / 'user',
                    '--work-dir', folder, '--headless', '--deterministic', '--translation', catalog,
                    '--report', report], cwd=folder, check=True, stdout=subprocess.DEVNULL, timeout=90)
    record = json.loads(report.read_text())
    assert not record['hsp_error'] and not record['first_unsupported'], report
    return [folder / 'user/user' / f'page{page}.bmp' for page in range(len(pages))]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for flag in ('host', 'compiler', 'catalog', 'ax-source', 'work'):
        ap.add_argument('--' + flag, type=Path, required=True)
    args = ap.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='fixed-log-', dir=args.work)).resolve()
    selected = set(re.findall(rb'native_log_identity = "@fixed-id/([0-9a-f]{20})"', args.ax_source.read_bytes()))
    pairs = json.loads((ROOT / 'work/localization/2.335/static-pairs.json').read_text())
    targets = {r['id']: r['target'] for r in json.loads((ROOT / 'src/localization/ko-KR/original-fixed-adventure.json').read_text())['entries']}
    actual, expected, keys = [], [], []
    captions = {}
    for row in pairs:
        pid = row['id']
        key = '@fixed-id/' + pid
        if pid not in targets or pid.encode() not in selected:
            continue
        for japanese, s in ((False, row['source']), (True, row['source_ja'])):
            if not s.strip():
                continue
            # The source logger capitalizes lowercase English before passing it on.
            if not japanese and s[0].isascii():
                s = s[0].upper() + s[1:]
            stamp = '[1:24] ' if len(keys) % 9 == 0 else ''
            s = stamp + s
            y = len(keys) % 32 * 20
            if not y:
                actual.append([])
                expected.append([])
            chunks = [s] if japanese else [w + ' ' for w in s.split(' ')]
            actual[-1].append(f'native_ko_log {quoted(s)},0,4096,0,{quoted(key)},{int(japanese)}')
            actual[-1] += [f'pos 8,{y} : msg_write {quoted(chunk)}' for chunk in chunks]
            ref = '@fixed-test/' + str(len(keys))
            captions[ref] = stamp + targets[pid]
            expected[-1].append(f'pos 8,{y} : mes {quoted(ref)}')
            keys.append((pid, japanese))
    assert len(keys) > 600
    catalog = run / 'test.tsv'
    def escape(value):
        return value.replace('\\', '\\\\').replace('\t', '\\t')
    catalog.write_bytes(args.catalog.read_bytes() + ''.join(f'{key}\t{escape(value)}\n' for key,value in captions.items()).encode())
    images = [render(run / name, args.host.resolve(), args.compiler.resolve(), catalog, pages)
              for name,pages in [('actual',actual),('expected',expected)]]
    for observed, reference in zip(*images):
        assert observed.read_bytes() == reference.read_bytes(), (observed, reference)
    result = {'distinct_source_pairs': len({p for p,_ in keys}), 'rendered': len(keys), 'pages': len(actual), 'passed': True}
    (run / 'summary.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result), run)


if __name__ == '__main__':
    main()
