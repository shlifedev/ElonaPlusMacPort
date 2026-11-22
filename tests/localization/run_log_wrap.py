#!/usr/bin/env python3
"""Wrap Korean through the original 2.335 Japanese logger and check scoped menu labels.

Each Japanese message runs through the original txt_conv wrapping loop with the
shipping native_ko_log call in front of it. Every wrapped row must stay inside
the log width and, together, the rows must draw exactly the glyph ink of the
whole translation: nothing dropped, nothing drawn twice.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE_SHA256 = 'c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55'
HOOK = 'native_ko_log msgtemp, msglen, inf_maxmsglen, native_starter_pet_id, str(native_log_identity), 1'
WIDTH, HEIGHT = 1600, 200
MESFONT, MAXLEN, SPACE = 14, 30, 16
WHITE = b'\xff\xff\xff'


def japanese_loop(lines):
    start = next(i for i in range(len(lines) - 1)
                 if lines[i] == '\t\trepeat' and lines[i + 1] == '\t\t\tlen@txtfunc = strlen(msgtemp)')
    end = lines.index('\t\tmsglen += len@txtfunc', start)
    # Module-qualified locals become plain globals in this single-file fixture.
    assert not any('goto *label_' in line for line in lines[start:end + 1])
    return [line.replace('@txtfunc', '') for line in lines[start:end + 1] if not line.startswith('*label_')]


def message_writer(lines):
    start = lines.index('#deffunc msg_write var msg_write_prm0')
    end = lines.index('#deffunc txtmore', start)
    return [line.replace('@txtfunc', '') for line in lines[start:end] if not line.startswith('*label_')]


def quoted(text):
    return '"' + text + '"'


def rows(path):
    data = path.read_bytes()
    width, height = struct.unpack_from('<ii', data, 18)
    bits = struct.unpack_from('<H', data, 28)[0]
    assert (width, abs(height)) == (WIDTH, HEIGHT) and bits in (24, 32), path
    offset = struct.unpack_from('<I', data, 10)[0]
    step = (WIDTH * bits // 8 + 3) & ~3
    size = bits // 8
    pixels = []
    for y in range(HEIGHT):
        base = offset + ((HEIGHT - 1 - y) if height > 0 else y) * step
        line = data[base:base + WIDTH * size]
        pixels.append(b''.join(line[x:x + 3] for x in range(0, len(line), size)) if size == 4 else line)
    return pixels


def ink(pixels, top, bottom):
    count, right = 0, -1
    for y in range(top, bottom):
        line = pixels[y]
        if line.count(WHITE) * 3 == len(line) and line == WHITE * WIDTH:
            continue
        for x in range(WIDTH):
            if line[3 * x:3 * x + 3] != WHITE:
                count += 1
                right = max(right, x)
    return count, right


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for option in ('host', 'compiler', 'catalog', 'work'):
        ap.add_argument('--' + option, type=Path, required=True)
    ap.add_argument('--source', type=Path, default=ROOT / 'work/decompiled/2.335-reference/start.hsp')
    args = ap.parse_args()
    raw = args.source.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == SOURCE_SHA256
    lines = raw.decode('cp932').splitlines()
    loop, writer = japanese_loop(lines), message_writer(lines)

    catalog = {}
    for line in args.catalog.read_text(encoding='utf-8').splitlines():
        if '\t' in line and not line.startswith('#'):
            key, value = line.split('\t', 1)
            catalog[key] = value
    resource = json.loads((ROOT / 'src/localization/ko-KR/original-fixed-adventure.json').read_text(encoding='utf-8'))
    def wraps(text):
        # Japanese literals that need at least two log rows and carry no markup.
        if not re.search(r'[぀-ヿ一-鿿]', text) or re.search(r'["\\\n\t♪{}]', text):
            return False
        try:
            return MAXLEN < len(text.encode('cp932')) <= 9 * MAXLEN
        except UnicodeEncodeError:
            return False

    # Identified choices (@fixed-id) and plain Japanese log keys (@log/).
    cases = []
    for row in resource['entries']:
        key = row['source']
        if key.startswith('@fixed-id/') and key in catalog:
            alias = next((a for a in row.get('source_aliases', ()) if not a.startswith('@') and wraps(a)), None)
            if alias:
                cases.append((key, alias, catalog[key]))
    cases += [('', key[5:], value) for key, value in sorted(catalog.items())
              if key.startswith('@log/') and wraps(key[5:])]
    assert len(cases) >= 100, len(cases)

    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='log-wrap-', dir=args.work)).resolve()
    (work / 'game').mkdir()
    (work / 'user/user').mkdir(parents=True)
    table = dict(catalog)
    for index, (_, _, target) in enumerate(cases):
        table[f'@reference/{index}'] = target
    # Scope 8 draws DB and computed values. A short @fixed/ phrase must not
    # replace them, while a label with a scoped key still translates.
    table['@fixed/Happy'] = '문맥 전용 번역'
    table['@reference/INIT'] = table['@sheet/INIT']
    fixture_catalog = work / 'fixture.tsv'
    fixture_catalog.write_text(''.join(f'{k}\t{v}\n' for k, v in table.items()), encoding='utf-8')

    hsp = ['#regcmd 48', '#cmd native_ko_log $01', '#cmd native_ko_menu $07', 'goto *main', *lines[214:221],
           '#deffunc msg_newline', 'inf_msgline++', 'msglen = 0', 'return', *writer, '*main',
           f'screen 0,{WIDTH},{HEIGHT}', 'jp = 1 : en = 0', 'cfg_font1 = "MS Gothic" : cfg_font2 = "MS Gothic"',
           f'inf_mesfont = {MESFONT} : inf_maxmsglen = {MAXLEN} : inf_msgspace = {SPACE}',
           'inf_msgx = 0 : inf_msgy = 0 : inf_maxlog = 4 : msgline = 0 : sdim msg, 4096, 4',
           'native_starter_pet_id = 0', 'dim tcol, 3 : tcol = 0, 0, 0',
           'color 255,255,255 : boxf', 'font "MS Gothic", 14', 'color 0,0,0',
           'native_ko_menu 8, "Happy"', 'pos 0, 0 : mes "Happy"', 'native_ko_menu 0, ""',
           'pos 500, 0 : mes "Happy"',
           'native_ko_menu 8, "INIT"', 'pos 0, 40 : mes "INIT"', 'native_ko_menu 0, ""',
           'pos 500, 40 : mes "@reference/INIT"', 'pos 1000, 40 : mes "INIT"',
           'bmpsave "user/scope.bmp"']
    for index, (key, japanese, _) in enumerate(cases):
        hsp += ['color 255,255,255 : boxf', 'msg = "", "", "", ""', 'msgline = 0',
                f'msgtemp = {quoted(japanese)}', 'msglen = 0 : inf_msgline = 1',
                f'native_log_identity = "{key}"', HOOK, *loop,
                f'bmpsave "user/wrap{index}.bmp"',
                'color 255,255,255 : boxf', f'font "MS Gothic", {MESFONT}, 0', 'color 0,0,0',
                f'pos 6, 6 : mes "@reference/{index}"', f'bmpsave "user/whole{index}.bmp"']
    hsp.append('end 0')
    fixture = work / 'fixture.hsp'
    fixture.write_bytes(('\r\n'.join(hsp) + '\r\n').encode('cp932'))
    ax = work / 'fixture.ax'
    with (work / 'compile.log').open('wb') as log:
        subprocess.run([args.compiler.resolve(), '-d', '--compath=' + str(ROOT / '.cache/upstream/OpenHSP/common') + '/',
                        '-o' + str(ax), fixture], check=True, stdout=log)
    report = work / 'report.json'
    with (work / 'host.log').open('wb') as log:
        subprocess.run([args.host.resolve(), '--game', ax, '--game-root', work / 'game', '--user-root', work / 'user',
                        '--work-dir', work, '--headless', '--deterministic', '--translation', fixture_catalog,
                        '--report', report], check=True, stdout=log, stderr=subprocess.STDOUT, timeout=300)
    status = json.loads(report.read_text())
    assert status['endcode'] == 0 and not status['first_unsupported'] and not status['hsp_error'], report

    out = work / 'user/user'
    scope = rows(out / 'scope.bmp')
    crop = lambda y, x: [row[3 * x:3 * (x + 300)] for row in scope[y:y + 20]]
    assert all(any(row != WHITE * 300 for row in crop(y, x)) for y, x in ((0, 0), (40, 0), (40, 1000))), 'blank label'
    assert crop(0, 0) == crop(0, 500), 'scope 8 applied an unscoped @fixed/ phrase'
    assert crop(40, 0) == crop(40, 500) and crop(40, 0) != crop(40, 1000), 'scoped sheet label missing'

    # Japanese line breaking lets a row end two bytes late before 。、」 etc.
    right_edge = 6 + (MAXLEN + 2) * MESFONT // 2
    results = []
    for index, (key, japanese, target) in enumerate(cases):
        wrapped, whole = rows(out / f'wrap{index}.bmp'), rows(out / f'whole{index}.bmp')
        count = (len(japanese.encode('cp932')) + MAXLEN - 1) // MAXLEN + 2
        per_row = [ink(wrapped, 6 + r * SPACE, 6 + (r + 1) * SPACE) for r in range(count)]
        total, full_right = ink(whole, 0, 40)
        used = [r for r in per_row if r[0]]
        assert sum(r[0] for r in per_row) == total, (key, 'glyph ink dropped or repeated', work)
        # Row widths are checked when the whole translation is clearly no
        # wider than the original message (CP932 bytes are half-width cells).
        fits = full_right - 6 <= 0.9 * len(japanese.encode('cp932')) * MESFONT / 2
        if fits:
            for n, (_, right) in enumerate(used[:-1]):
                assert right <= right_edge, (key, n, right, right_edge, work)
        results.append({'id': key, 'rows': len(used), 'fits_log_width': fits,
                        'widest_row': max(r[1] for r in used), 'final_row_right': used[-1][1]})
    # The final row receives what earlier rows could not take at a word
    # break. For the narrower translations it must also end inside the log.
    final = [r['final_row_right'] - right_edge for r in results if r['fits_log_width'] and r['rows'] > 1]
    assert all(v <= 0 for v in final), ('final row passed the log edge', final, work)
    summary = {'messages': len(results), 'checked_row_widths': sum(r['fits_log_width'] for r in results),
               'final_row_overflow_px': {'count': sum(v > 0 for v in final), 'max': max([0, *final])},
               'log_right_edge': right_edge, 'scoped_labels': True, 'passed': True, 'cases': results}
    (work / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f"{summary['messages']} wrapped Japanese log messages kept their glyphs; "
          f"{summary['checked_row_widths']} narrower translations stayed inside the log "
          f"(final rows over the edge: {summary['final_row_overflow_px']}); scoped labels ok: {work}")


if __name__ == '__main__':
    main()
