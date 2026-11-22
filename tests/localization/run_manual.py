#!/usr/bin/env python3
"""Render every stock help topic through the same lookup and box used by the game."""
import argparse
import json
import re
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / 'build/native-macos-arm64-legacy-cp932-release'


def glyph_pixels(path, x, y, width, height):
    data = path.read_bytes()
    offset = struct.unpack_from('<I', data, 10)[0]
    w, h, _, depth = struct.unpack_from('<iiHH', data, 18)
    assert depth == 24 and x + width <= w and y + height <= h
    stride = (w * 3 + 3) & ~3
    ink, edge = set(), set()
    colors = (b'\x1e\x1e\x1e', b'\xff\x32\x32')
    near = {bytes((b, g, r)): base for base in colors
            for b in range(max(0, base[0] - 1), min(256, base[0] + 2))
            for g in range(base[1] - 1, base[1] + 2) for r in range(base[2] - 1, base[2] + 2)}
    for dy in range(height):
        start = offset + (h - 1 - (y + dy)) * stride + 3 * x
        row = data[start:start + 3 * width]
        for dx in range(width):
            color = row[3 * dx:3 * dx + 3]
            if color in colors:
                ink.add((dx, dy, color))
            if color in near:
                edge.add((dx, dy, near[color]))
    return ink, edge


def play_topics(args, run, edition, headings, heights):
    work = run / edition
    (work / 'shots').mkdir(parents=True)
    (work / 'profile').mkdir()
    route = ['120 key DOWN'] if edition == 'ENG' else []
    route += ['140 key RETURN', '420 key B', '480 key F1', '520 key A', '550 screenshot shots/keys.bmp']
    frame = 580
    for slot, heading in enumerate(headings, 1):
        if slot % 18 == 0:
            route.append(f'{frame} key 107')  # numeric keypad +
            frame += 40
        route += [f'{frame} key {chr(ord("A") + slot % 18)}',
                  f'{frame + 25} screenshot shots/{heading}.bmp']
        frame += 40
    route += [f'{frame} key ESCAPE', f'{frame + 40} exit 0']
    (work / 'route.script').write_text('\n'.join(route) + '\n')
    with (work / 'host.log').open('wb') as log:
        subprocess.run([args.host.resolve(), '--game', args.ax.resolve(), '--game-root', ROOT / 'elonaplus2.335',
                        '--packed-dir', ROOT / 'work/extracted/2.335', '--work-dir', work, '--user-root', work / 'profile',
                        '--headless', '--deterministic', '--script', work / 'route.script', '--max-waits', str(frame + 55),
                        '--translation', args.catalog.resolve(), '--translation-misses', work / 'misses.tsv',
                        '--report', work / 'report.json'], check=True, stdout=log, stderr=log)
    report = json.loads((work / 'report.json').read_text())
    assert report['frames'] == frame + 40 and not report['observed_hsp_errors'] and not report['first_unsupported']
    untranslated = [line for line in (work / 'misses.tsv').read_text().splitlines()[1:]
                    if re.search(r'[A-Za-z]{3}|[ぁ-ヿ一-龯]', line.split('\t')[0])]
    assert not untranslated, (edition, untranslated)
    for heading in headings:
        height = heights[(edition, heading)]
        wanted, wanted_edge = glyph_pixels(run / f'profile/user/{edition}-{heading}.bmp', 16, 10, 510, height)
        shown, shown_edge = glyph_pixels(work / f'shots/{heading}.bmp', 226, 112, 510, height)
        # Nearly opaque antialiased pixels can round one channel differently
        # on the real parchment and the white fixture. Compare at the same xy.
        assert wanted and shown and wanted <= shown_edge and shown <= wanted_edge, (edition, heading)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, default=BIN / 'elona_hspcmp')
    parser.add_argument('--host', type=Path, default=BIN / 'elona_gui_host')
    parser.add_argument('--catalog', type=Path, default=ROOT / 'src/localization/ko-KR/runtime.tsv')
    parser.add_argument('--ax', type=Path, help='Also open all topics in the real game and compare their glyph pixels.')
    parser.add_argument('--work', type=Path, default=ROOT / 'work/ko-manual')
    args = parser.parse_args()
    args.work.mkdir(exist_ok=True, parents=True)
    run = Path(tempfile.mkdtemp(dir=args.work)).resolve()
    (run / 'profile').mkdir()
    game = ROOT / 'elonaplus2.335'
    script = ['#regcmd 48', '#cmd native_ko_menu $07', '#cmd native_ko_gmes $00',
              'screen 0, 550, 420', 'redraw 0', 'sdim heights, 1024']
    expected = []
    for edition in ('ENG', 'JP'):
        filename = f'manual_{edition}.txt'
        # HSP notes index from zero. The menu stores the first body line.
        heads = [n for n, line in enumerate((game / 'data' / filename).read_bytes().splitlines(), 1)
                 if line.startswith(b'{} ')]
        assert len(heads) == (49 if edition == 'ENG' else 50)
        script += ['notesel body', f'noteload "data\\\\{filename}"', 'native_ko_menu 22, body']
        for heading in heads:
            script += ['color 255,255,255 : boxf',
                       f'native_ko_gmes "@manual/page/{heading}",16,10,510,0,30,30,30',
                       'if stat < 18 : end 90', 'if stat > 390 : end 91',
                       'heights += str(stat) + ","']
            expected.append((edition, heading))
            if args.ax:
                script += [f'bmpsave "user\\\\{edition}-{heading}.bmp"']
        # Changing one topic must not substitute its stock translation, even
        # if a modified edition uses the same headings and section offsets.
        script += [f'noteadd "Changed by the reader", {heads[0] + 1}, 1', 'native_ko_menu 22, body',
                   f'native_ko_gmes "@manual/page/{heads[0]}",16,10,510,0,30,30,30',
                   'if stat >= 0 : end 92',
                   f'native_ko_gmes "@manual/page/{heads[1]}",16,10,510,0,30,30,30',
                   'if stat < 0 : end 93', 'native_ko_menu 22, ""',
                   f'native_ko_gmes "@manual/page/{heads[1]}",16,10,510,0,30,30,30',
                   'if stat >= 0 : end 94']
    script += ['notesel heights', 'notesave "user\\\\heights.txt"', 'await 1', 'end 0']
    (run / 'manual.hsp').write_bytes(('\r\n'.join(script) + '\r\n').encode('ascii'))
    common = ROOT / '.cache/upstream/OpenHSP/common'
    with (run / 'compile.log').open('wb') as log:
        subprocess.run([args.compiler.resolve(), f'--compath={common}/', f'-o{run}/manual.ax',
                        run / 'manual.hsp'], cwd=run, check=True, stdout=log, stderr=log)
    with (run / 'host.log').open('wb') as log:
        subprocess.run([args.host.resolve(), '--game', run / 'manual.ax', '--game-root', game,
                        '--user-root', run / 'profile', '--work-dir', run, '--headless', '--deterministic',
                        '--translation', args.catalog.resolve(), '--report', run / 'report.json'],
                       check=True, stdout=log, stderr=log)
    report = json.loads((run / 'report.json').read_text())
    assert not report['first_unsupported'] and not report['observed_hsp_errors']
    heights = [int(x) for x in (run / 'profile/user/heights.txt').read_text().strip().split(',') if x]
    assert len(heights) == len(expected) and min(heights) >= 18 and max(heights) <= 390
    if args.ax:
        measured = dict(zip(expected, heights))
        for edition in ('ENG', 'JP'):
            play_topics(args, run, edition, [n for lang, n in expected if lang == edition], measured)
    result = {'topics': len(expected), 'min_height': min(heights), 'max_height': max(heights),
              'available_height': 390, 'modified_topics_preserved': 2,
              'real_game_pages_compared': len(expected) if args.ax else 0, 'run': str(run)}
    (run / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
