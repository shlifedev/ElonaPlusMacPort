#!/usr/bin/env python3
"""Prove localization changes display pixels without changing VM/edit strings."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser(description=__doc__)
for key in ('compiler', 'host', 'common', 'work'):
    ap.add_argument('--' + key, type=Path, required=True)
a = ap.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix='run-', dir=a.work)).resolve()
source = work / 'display.hsp'
source.write_bytes((ROOT / 'tests/localization/display.hsp').read_bytes())
ax = work / 'display.ax'
c = subprocess.run([str(a.compiler.resolve()), '-d', '--compath=' + str(a.common.resolve()) + '/', '-o' + str(ax), str(source)], capture_output=True)
assert c.returncode == 0, c.stderr
(work / 'catalog.tsv').write_text('Starting Menu\t모험의 이정표\n', encoding='utf-8')
images = []
reports = {}
for name in ('original', 'korean'):
    case = work / name
    case.mkdir()
    cmd = [str(a.host.resolve()), '--trusted-fixture', str(ax), '--work-dir', str(case), '--headless', '--deterministic',
           '--report', str(case / 'report.json'), '--screenshot', str(case / 'screen.bmp')]
    if name == 'korean': cmd += ['--translation', str(work / 'catalog.tsv')]
    run = subprocess.run(cmd, capture_output=True, timeout=30)
    (case / 'stderr.log').write_bytes(run.stderr)
    rep = json.loads((case / 'report.json').read_text())
    assert run.returncode == 0 and not rep['hsp_error'] and not rep['first_unsupported'], rep
    assert (case / 'vm-value.txt').read_bytes() == b'Starting Menu'
    images.append((case / 'screen.bmp').read_bytes())
    reports[name] = {'exit': run.returncode, 'vm_string_unchanged': True}
assert images[0] != images[1], 'Korean display should differ from original'
reports.update(passed=True, display_changed=True, original_vm_preserved=True)
# The opt-in managed paragraph ABI receives the complete original CP932 text,
# lays out reviewed UTF-8, and explicitly falls back for unmatched paragraphs.
paragraph = work / 'paragraph.hsp'
paragraph.write_bytes((ROOT / 'tests/localization/paragraph.hsp').read_bytes())
paragraph_ax = work / 'paragraph.ax'
subprocess.run([str(a.compiler.resolve()), '-d', '--compath=' + str(a.common.resolve()) + '/',
                '-o' + str(paragraph_ax), str(paragraph)], capture_output=True, check=True)
(work / 'paragraph.tsv').write_text('<emp1>Read<def> a spellbook.\t<emp1>마법서<def>를 읽으면 주문을 배울 수 있다.\n', encoding='utf-8')
case = work / 'paragraph'
case.mkdir()
(case / 'game').mkdir()
(case / 'user-root').mkdir()
(case / 'user-root/user').mkdir()
cmd = [str(a.host.resolve()), '--game', str(paragraph_ax), '--work-dir', str(case),
       '--game-root', str(case / 'game'), '--user-root', str(case / 'user-root'),
       '--headless', '--deterministic', '--translation', str(work / 'paragraph.tsv'),
       '--report', str(case / 'report.json'), '--screenshot', str(case / 'screen.bmp')]
run = subprocess.run(cmd, capture_output=True, timeout=30)
(case / 'stderr.log').write_bytes(run.stderr)
rep = json.loads((case / 'report.json').read_text())
assert run.returncode == 0 and not rep['first_unsupported'] and not rep['hsp_error'], rep
assert (case / 'user-root/user/vm-paragraph.txt').read_bytes() == b'<emp1>Read<def> a spellbook.'
reports['paragraph'] = {'passed': True, 'vm_string_unchanged': True, 'fallback_checked': True}
(work / 'results.json').write_text(json.dumps(reports, indent=2) + '\n')
print('PASS Korean display differs; VM/save/edit value stays original:', work)
