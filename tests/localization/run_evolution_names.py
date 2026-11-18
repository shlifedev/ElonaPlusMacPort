#!/usr/bin/env python3
"""Check all 2.335 evolution identities and render JP/EN success messages."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from korean_evolution_hooks import install_evolution_hooks


def inspect_source(source, rows):
    assert hashlib.sha256(source).hexdigest() == 'c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55'
    lines = source.decode('cp932').splitlines()
    conditions, routes = {}, {}
    for i in range(366723, 368747):
        line = lines[i - 1]
        depth = len(line) - len(line.lstrip())
        command = line.strip()
        if command.startswith('if ('):
            conditions = {d: c for d, c in conditions.items() if d < depth}
            conditions[depth] = command
        match = re.fullmatch(r'ev(old|name) = lang\("([^"]+)", "([^"]+)"\)', command)
        if not match:
            continue
        branch = [c for d, c in conditions.items() if d < depth]
        mode = re.fullmatch(r'if \( evmode == (\d+) \) \{', branch[0])[1]
        key = '@evolution/' + mode
        if len(branch) > 1:
            assert len(branch) == 2
            stat, value = re.fullmatch(r'if \( cdata\((27|214), tc\) == (\d+) \) \{', branch[1]).groups()
            key += ('/race/' if stat == '27' else '/stage/') + value
        key += '/old' if match[1] == 'old' else '/new'
        assert key not in routes
        routes[key] = (i, match[2], match[3])
    assert len(routes) == len(rows) == 421 and len({k.split('/')[1] for k in routes}) == 167
    for row in rows:
        assert row['source_ref'] == '2.335:start.hsp:' + str(routes[row['source']][0])
        assert re.search('[가-힣]', row['target'])
    hooks = {}

    def collect(anchor, addition, kind, *, after=False):
        assert source.count(anchor) == 1
        hooks[kind] = addition.decode('cp932').strip()
        assert after == (kind == 'evolution_success_end')
    install_evolution_hooks(collect)
    assert len(hooks) == 3
    return lines, routes, hooks


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, default=ROOT / 'work/decompiled/2.335-reference/start.hsp')
    for option in ('compiler', 'host', 'work'):
        ap.add_argument('--' + option, type=Path)
    a = ap.parse_args()
    rows = json.loads((ROOT / 'localization/ko-KR/original-evolution-ui.json').read_text())['entries']
    lines, routes, hooks = inspect_source(a.source.read_bytes(), rows)
    if not any((a.compiler, a.host, a.work)):
        print('167 evolution modes, 421 source references and three original hook anchors verified')
        return
    assert all((a.compiler, a.host, a.work))
    a.work.mkdir(exist_ok=True, parents=True)
    work = Path(tempfile.mkdtemp(dir=a.work, prefix='evolution-')).resolve()
    targets = {row['source']: row['target'] for row in rows}
    cases = [(0, 1, 0, 0, '', ''), (1, 1, 0, 0, '', ''), (1, 1, 0, 0, '', 'aaaaaaa Fox'),
             (0, 1, 0, 0, '', '妹猫の花'), (1, 3, 267, 0, '/race/267', 'Feykka the dog'),
             (1, 101, 108, 1, '/stage/1', 'Fox'), (0, 101, 108, 2, '/stage/2', '')]
    for variant in ('actual', 'expected'):
        folder = work / variant
        (folder/'game').mkdir(parents=True)
        (folder/'user/user').mkdir(parents=True)
        code = ['#regcmd 48', '#cmd native_ko_log $01', 'goto *main', *lines[214:221],
                '#deffunc msg_write str chunk', 'mes chunk', 'return', '*main',
                'screen 0,960,360', 'color 255,255,255 : boxf', 'color 0,0,0',
                'font "MS Gothic",14', 'dim cdata,440,2', 'sdim cdatan,128,4,2', 'tc = 1']
        for index, (en, mode, race, stage, selector, nick) in enumerate(cases):
            base = f'@evolution/{mode}'
            old = base + selector + '/old'
            new = base + '/new'
            saved = nick or routes[old][1 + en]
            korean = ('『' + nick + '』 진화! ') if nick else '동료 진화! '
            target = '축하합니다! ' + korean + targets[old] + ' → ' + targets[new]
            targets[f'@expected/{index}'] = target
            code += [f'en = {en}', f'jp = {1-en}', f'evmode = {mode}', f'cdata(27,tc) = {race}',
                     f'cdata(214,tc) = {stage}', f'cdatan(0,tc) = "{saved}"',
                     hooks['evolution_prior_stage'], lines[routes[old][0]-1], lines[routes[new][0]-1]]
            if mode == 101:
                code.append('cdata(214,tc) = 3')
            code += [hooks['evolution_success_begin'], f'pos 8,{index*38 + 8}']
            raw = lines[368751].split('txt_select ', 1)[1].split('), "",', 1)[0] + ')'
            code.append('message = ' + raw)
            if variant == 'expected':
                code.append(f'mes "@expected/{index}"')
            else:
                code.append('native_ko_log message,0,240,0,native_log_identity,1-en')
                if en:
                    for word in 'Congratulations! Your friend has evolved!'.split():
                        code += [f'pos 8,{index*38 + 8}', f'msg_write "{word} "']
                else:
                    code.append('msg_write message')
            code += [hooks['evolution_success_end'], 'if native_log_identity != "" : end 6',
                     f'if cdatan(0,tc) != "{saved}" : end 7']
        code += ['bmpsave "user/frame.bmp"', 'end 0']
        src, ax = folder/'fixture.hsp', folder/'fixture.ax'
        src.write_bytes(('\r\n'.join(code)+'\r\n').encode('cp932'))
        subprocess.run([a.compiler.resolve(), '-d', '--compath='+str(ROOT/'.cache/upstream/OpenHSP/common')+'/',
                        '-o'+str(ax), src], check=True, stdout=(folder/'compile.log').open('wb'))
        catalog = folder/'fixture.tsv'
        catalog.write_text(''.join(f'{k}\t{v}\n' for k,v in targets.items()), encoding='utf-8')
        report = folder/'report.json'
        with (folder/'host.log').open('wb') as log:
            subprocess.run([a.host.resolve(), '--game', ax, '--game-root', folder/'game', '--user-root', folder/'user',
                            '--work-dir', folder, '--headless', '--deterministic', '--translation', catalog,
                            '--report', report], check=True, stdout=log, stderr=subprocess.STDOUT, timeout=30)
        status = json.loads(report.read_text())
        assert status['endcode'] == 0 and not status['first_unsupported'] and not status['hsp_error'], report
    actual = next((work/'actual').rglob('frame.bmp'))
    expected = next((work/'expected').rglob('frame.bmp'))
    assert actual.read_bytes() == expected.read_bytes(), work
    print(f'421 source routes and {len(cases)} Korean success messages verified: {work}')


if __name__ == '__main__':
    main()
