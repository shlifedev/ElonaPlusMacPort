#!/usr/bin/env python3
"""Replay English-based 2.335 alias selection, two rerolls, save and reload on macOS."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from inspect_save_state import digest, payload
from play_game import MANAGED_AX

PAGES = ('initial', 'reroll1', 'reroll2')
# 800x600 English layout from the creation cs_list and character sheet mes.
# Excludes title, shortcut letters, stat labels and neighbouring rows.
ROWS = [(264, 176 + 19*i, 540, 192 + 19*i) for i in range(16)]
SHEET = (118, 146, 265, 160)


def creation_script():
    lines = []
    for line in (ROOT / 'tests/game/newgame_english.script').read_text().splitlines():
        if line == '500 screenshot shots/f0500-alias.bmp':
            lines.extend(['445 screenshot shots/initial.bmp', '455 key A',
                          '470 screenshot shots/reroll1.bmp', '480 key A',
                          '500 screenshot shots/reroll2.bmp'])
        elif 's2070-sheet.bmp' in line:
            lines.append('2070 screenshot shots/sheet.bmp')
        elif ' screenshot ' not in line and ' capture ' not in line:
            lines.append(line)
    return '\n'.join(lines) + '\n'


RELOAD = '''320 key A
420 key A
740 key C
810 screenshot shots/sheet.bmp
840 key ESCAPE
900 exit 0
'''


class Screen:
    def __init__(self, path):
        self.bmp = path.read_bytes()
        layout = struct.unpack_from('<iiHHI', self.bmp, 18)
        if self.bmp[:2] != b'BM' or layout != (800, 600, 1, 24, 0):
            raise ValueError(f'unexpected BMP layout in {path}')
        self.offset = struct.unpack_from('<I', self.bmp, 10)[0]
        if len(self.bmp) < self.offset + 600 * 800 * 3:
            raise ValueError(f'incomplete BMP: {path}')

    def scanlines(self, rect):
        left, top, right, bottom = rect
        for y in range(top, bottom):
            start = self.offset + ((599-y)*800 + left)*3
            yield slice(start, start+(right-left)*3)

    def region(self, rect):
        return b''.join(self.bmp[line] for line in self.scanlines(rect))

    def backdrop(self):
        # Erasing only the alias text must restore identical page backgrounds.
        result = bytearray(self.bmp)
        for rect in ROWS:
            for line in self.scanlines(rect):
                result[line] = bytes(line.stop-line.start)
        return result


def run_host(args, stage, ax, script, catalog=None, source_profile=None):
    (stage/'cwd/shots').mkdir(parents=True)
    if source_profile:
        shutil.copytree(source_profile, stage/'profile')
    else:
        (stage/'profile').mkdir()
    script_path = stage/'input.script'
    script_path.write_text(script)
    command = [args.host, '--game', ax, '--game-root', ROOT/'elonaplus2.335',
               '--packed-dir', ROOT/'work/extracted/2.335', '--user-root', stage/'profile',
               '--work-dir', stage/'cwd', '--headless', '--deterministic',
               '--script', script_path, '--max-waits', '4700', '--timeout-ms', '100000',
               '--report', stage/'report.json']
    if catalog:
        command.extend(['--translation', catalog, '--translation-fallback', args.fallback,
                        '--translation-misses', stage/'misses.tsv'])
    with (stage/'host.log').open('wb') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=150)
    report = json.loads((stage/'report.json').read_text())
    expected = ('script-exit', 0) if ' exit 0' in script else ('max-waits', 124)
    if ((report['exit_reason'], result.returncode) != expected or report['exit_code'] != result.returncode
            or report['first_unsupported'] or report['observed_hsp_error_count'] or report['hsp_error']):
        raise RuntimeError(f'unexpected host result in {stage}: {report["exit_reason"]}, {result.returncode}')
    print(stage.name, report['frames'], flush=True)
    return report['frames']


def saves(stage):
    root = stage/'profile/save'
    return {p.relative_to(root).as_posix(): digest(payload(p)) for p in root.rglob('*') if p.is_file()}


def missed_sources(stage):
    return {line.rsplit('\t', 1)[0] for line in (stage/'misses.tsv').read_text(encoding='utf-8').splitlines()
            if '\t' in line}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--host', type=Path, default=ROOT/'build/native-macos-arm64-legacy-cp932-release/elona_gui_host')
    ap.add_argument('--ax', type=Path, default=ROOT/MANAGED_AX)
    ap.add_argument('--reference', type=Path, default=ROOT/'work/normalized/2.335/start.ax')
    ap.add_argument('--catalog', type=Path, default=ROOT/'src/localization/ko-KR/runtime.tsv')
    ap.add_argument('--fallback', type=Path, default=ROOT/'src/localization/ko-KR/draft-display.tsv')
    args = ap.parse_args()
    work = ROOT/'work/ko-alias-selection'
    work.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(dir=work))
    print(run, flush=True)
    # Control changes only the alias dictionary; labels, font and backdrop stay identical.
    control = run/'no-alias.tsv'
    control.write_bytes(b''.join(line for line in args.catalog.read_bytes().splitlines(keepends=True)
                                 if not line.startswith((b'@alias-word/', b'@alias-noun/'))))
    configs = [('reference', args.reference, None), ('control', args.ax, control),
               ('korean', args.ax, args.catalog)]
    frames = {}
    for label, ax, catalog in configs:
        frames[label] = run_host(args, run/label, ax, creation_script(), catalog)
    for label, source, catalog in [('reload-reference', 'reference', None),
                                    ('reload-control', 'reference', control),
                                    ('reload-original', 'reference', args.catalog),
                                    ('reload-korean', 'korean', args.catalog)]:
        ax = args.ax if catalog else args.reference
        frames[label] = run_host(args, run/label, ax, RELOAD, catalog, run/source/'profile')

    # The fifth candidate after the first reroll has a Korean label wider
    # than the original English highlight. Move the cursor without choosing it.
    focus_script = creation_script().split('480 key A')[0]
    focus_script += ''.join(f'{480 + i*20} key DOWN\n' for i in range(5))
    focus_script += '580 screenshot shots/focused.bmp\n600 exit 0\n'
    for label, ax, catalog in [('cursor-reference', args.reference, None),
                               ('cursor-no-catalog', args.ax, None),
                               ('cursor-korean', args.ax, args.catalog)]:
        frames[label] = run_host(args, run/label, ax, focus_script, catalog)

    def screen(label, page):
        return Screen(run/label/'cwd/shots'/f'{page}.bmp')

    def sheet(label):
        return screen(label, 'sheet').region(SHEET)

    results, korean_pages = {}, []
    backgrounds_equal = True
    for page in PAGES:
        korean, english = screen('korean', page), screen('control', page)
        results[page] = [korean.region(rect) != english.region(rect) for rect in ROWS]
        korean_pages.append(tuple(digest(korean.region(rect)) for rect in ROWS))
        backgrounds_equal &= korean.backdrop() == english.backdrop()
    selected = next((run/'reference/profile/save').glob('*/cdatan.s1')).read_bytes().split(b'\r\n')[1].decode('cp932')
    source_hashes = saves(run/'reference')
    reload_hashes = saves(run/'reload-reference')
    checks = {
        'creation_frames_equal': len({frames[label] for label, _, _ in configs}) == 1,
        'reload_frames_equal': len({f for label, f in frames.items() if label.startswith('reload-')}) == 1,
        'creation_save_payloads_equal': bool(source_hashes) and all(saves(run/label) == source_hashes
                                                                    for label, _, _ in configs),
        'reload_save_payloads_equal': all(saves(run/label) == reload_hashes for label in frames
                                          if label.startswith('reload-')),
        'all_candidate_rows_changed': all(all(values) for values in results.values()),
        'unchanged_outside_candidate_text': backgrounds_equal,
        'rerolls_changed_pages': len(set(korean_pages)) == 3,
        'creation_sheet_alias_translated': sheet('korean') != sheet('control'),
        'original_save_sheet_alias_translated': sheet('reload-original') != sheet('reload-control'),
        'both_saves_show_same_alias': sheet('reload-korean') == sheet('reload-original'),
        'selected_alias_exercised': all(selected in missed_sources(run/stage)
                                        for stage in ('control', 'reload-control')),
        'selected_alias_translated': all(selected not in missed_sources(run/stage)
                                         for stage in ('korean', 'reload-original', 'reload-korean')),
    }
    # Sample the invariant center scanline of the cursor sprite at its stock
    # English Reroll position (cs_list: x + strlen(source)*7 + 32 - 20).
    row_left = ROWS[0][0]
    x = row_left + len('Reroll')*7 + 32 - 20
    sprite = screen('cursor-reference', 'initial').region((x, 165, x+10, 166))

    def pointer(label, page, row):
        y = 165 + row*19
        scanline = screen(label, page).region((row_left, y, 590, y+1))
        matches = [row_left+i for i in range(len(scanline)//3)
                   if scanline[i*3:i*3+len(sprite)] == sprite]
        if len(matches) != 1:
            raise ValueError(f'cannot identify cursor on {label}/{page} row {row}')
        return matches[0]

    pointers = {}
    for label in ('cursor-reference', 'cursor-no-catalog', 'cursor-korean'):
        pointers[label] = [pointer(label, 'reroll1', 0), pointer(label, 'focused', 5)]
    checks['no_catalog_preserves_original_cursor'] = pointers['cursor-reference'] == pointers['cursor-no-catalog']
    # Read the rightmost ink of each *unselected* label so a cursor hiding
    # the final letter on the selected row cannot make this check pass.
    for name, page, row, idx in [('reroll', 'focused', 0, 0), ('candidate', 'reroll1', 5, 1)]:
        top = 157 + row*19
        rect = (row_left, top, 540, top+16)
        ink = screen('cursor-korean', page).region(rect)
        right = max(row_left+(i//3)%(rect[2]-row_left)
                    for i in range(0, len(ink), 3) if max(ink[i:i+3]) < 48)
        checks[name+'_cursor_clears_korean_text'] = pointers['cursor-korean'][idx] > right
        checks[name+'_cursor_widens_original'] = pointers['cursor-korean'][idx] > pointers['cursor-reference'][idx]
    summary = dict(checks=checks, frames=frames, rows=results, pointers=pointers, saved_alias=selected,
                   save_files=len(source_hashes), inputs={name: digest(getattr(args, name).read_bytes())
                        for name in ('host','ax','reference','catalog','fallback')})
    (run/'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(checks)
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
