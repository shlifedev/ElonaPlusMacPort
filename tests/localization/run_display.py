#!/usr/bin/env python3
"""Prove localization changes display pixels without changing VM/edit strings."""
import argparse
import hashlib
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
(work / 'catalog.tsv').write_text('Starting Menu\t모험의 이정표\n@dialog/Starting Menu\t대화상자 제목\n', encoding='utf-8')
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
    expected_dialog = '대화상자 제목' if name == 'korean' else 'Starting Menu'
    assert rep['dialogs'][0]['title'] == expected_dialog
    assert rep['dialogs'][0]['text'].splitlines() == [expected_dialog, 'Untranslated detail']
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
# Draw a Japanese two-glyph fallback inside the original gmes convention,
# and the identical first glyph as a standalone label after returning.
glyph_source = work / 'glyph.hsp'
glyph_source.write_bytes("""#regcmd 48
#cmd native_ko_gmes $00
screen 0,320,200
goto *main
#deffunc gmes str paragraph
value = paragraph
font "MS Gothic",14
pos 8,8 : mes strmid(value,0,2)
pos 22,8 : mes strmid(value,2,2)
return
*main
gmes "猫舌"
pos 8,64 : mes "猫"
native_ko_gmes "@home-name/title/Drunken Ram",8,96,290,0,0,0,0
native_ko_gmes "@home-name/player/Drunken Ram",8,120,290,0,0,0,0
native_ko_gmes "Expected alias",8,144,290,0,0,0,0
native_ko_gmes "Expected player name",8,168,290,0,0,0,0
await 1
end
""".encode('cp932'))
glyph_ax = work / 'glyph.ax'
subprocess.run([str(a.compiler.resolve()), '--compath=' + str(a.common.resolve()) + '/',
                '-o' + str(glyph_ax), str(glyph_source)], capture_output=True, check=True)
(work / 'glyph.tsv').write_text(
    '猫\t고양이\n@home-name/title\t{alias}\n@home-name/player\t{name}\n'
    'Expected alias\t술에 취한 숫양\nExpected player name\tDrunken Ram\n'
    + '@alias-word/' + hashlib.sha256(b'drunken').hexdigest() + '\t|술에 취한\n'
    + '@alias-noun/' + hashlib.sha256(b'ram').hexdigest() + '\t숫양\n', encoding='utf-8')
from struct import unpack_from
def bitmap_rows(path):
    data = path.read_bytes()
    offset = unpack_from('<I', data, 10)[0]
    width, height = unpack_from('<ii', data, 18)
    bits = unpack_from('<H', data, 28)[0]
    stride = (width * bits + 31) // 32 * 4
    rows = [data[offset+i*stride:offset+(i+1)*stride] for i in range(abs(height))]
    return rows[::-1] if height > 0 else rows

images = []
for name in ('raw', 'ko'):
    case = work / ('glyph-' + name)
    case.mkdir()
    (case / 'user-root').mkdir()
    command = [str(a.host.resolve()), '--game', str(glyph_ax), '--work-dir', str(case),
               '--game-root', str(work / 'paragraph/game'), '--user-root', str(case / 'user-root'),
               '--headless', '--deterministic', '--screenshot', str(case / 'screen.bmp')]
    if name == 'ko':
        command += ['--translation', str(work / 'glyph.tsv')]
    subprocess.run(command, check=True, capture_output=True, timeout=30)
    images.append(bitmap_rows(case / 'screen.bmp'))
assert any(row != images[0][0] for row in images[0][:48]), 'original fallback must draw visible glyphs'
assert images[0][:48] == images[1][:48], 'fallback glyphs must retain original pixels'
assert images[0][48:96] != images[1][48:96], 'standalone label must translate'
ko = images[1]
assert ko[96:116] == ko[144:164] and ko[120:140] == ko[168:188] and ko[96:116] != ko[120:140], \
    'the same saved bytes must render as Korean in an alias field and literally in a name field'
reports['gmes_fragment'] = {'fallback_pixels_equal': True, 'standalone_translated': True}
(work / 'results.json').write_text(json.dumps(reports, indent=2) + '\n')
print('PASS Korean display differs; VM/save/edit value stays original:', work)
