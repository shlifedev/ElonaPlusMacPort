#!/usr/bin/env python3
"""Exercise the lost-kitten quest on disposable copies of the scripted Vernis save."""
import argparse
import gzip
import json
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from inspect_save_state import digest, payload
from play_game import MANAGED_AX

# 2.335: reach the town and save using tests/game/world_to_vernis.script.
# Use the same input for both runs, speaking to the NPC immediately south.
SCRIPT = '''320 key A
420 key A
740 key DOWN
810 screenshot shots/npc.bmp
870 key RETURN
950 screenshot shots/journal.bmp
990 key ESCAPE
1100 key A
1250 key RETURN
1500 exit 0
'''


def change_fields(path, fields):
    data = bytearray(payload(path))
    for index, (expected, value) in fields.items():
        if struct.unpack_from('<i', data, 4*index)[0] != expected:
            raise ValueError(f'expected the scripted 2.335 Vernis fixture in {path.name}, field {index}')
        struct.pack_into('<i', data, 4*index, value)
    if path.read_bytes().startswith(b'\x1f\x8b'):
        data = gzip.compress(data, mtime=0)
    path.write_bytes(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile', required=True, type=Path, help='profile produced by the English Vernis replay')
    ap.add_argument('--host', type=Path, default=ROOT/'build/native-macos-arm64-legacy-cp932-release/elona_gui_host')
    ap.add_argument('--ax', type=Path, default=ROOT/MANAGED_AX)
    ap.add_argument('--catalog', type=Path, default=ROOT/'localization/ko-KR/runtime.tsv')
    args = ap.parse_args()
    evidence = ROOT/'work/ko-town-quest'
    evidence.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(dir=evidence))
    fixture = work/'fixture'
    shutil.copytree(args.profile, fixture)
    slot = fixture/'save/sav_Ari'
    # start.hsp:23839 has 500 int32s per character. Put the player beside
    # Vernis map character #2; use its normal chat dispatch (370465-370470)
    # with the dog constable's dialogue ID (385773). No changes to quest state.
    change_fields(slot/'cdata.s1', {1:(26,24), 2:(1,4), 3:(26,24), 4:(23,4)})
    row = 2*500
    town = slot/'cdata_5_101.s2'
    change_fields(town, {row+27:(30,369), row+150:(3,0)})
    if struct.unpack_from('<i', payload(slot/'gdata.s1'), 4*475)[0] != 0:
        raise ValueError('this fixture has already started the lost-kitten quest')

    # Remove only keys owned by this conversation to isolate the actual text
    # area. The managed AX, source data, font and all other labels stay equal.
    reviewed = json.loads((ROOT/'localization/ko-KR/original-quest-dialogue.json').read_text())
    rows = [row for row in reviewed if '2.335:start.hsp:385777' in row['source_ref']]
    if len(rows) != 1:
        raise ValueError('expected one reviewed introduction for dialogue 369')
    keys = {rows[0]['source'], rows[0]['source_ja']}
    control = work/'control.tsv'
    control.write_text(''.join(line for line in args.catalog.read_text().splitlines(keepends=True)
                               if line.split('\t', 1)[0] not in keys))
    (work/'input.script').write_text(SCRIPT)
    hashes, reports, pictures = [], [], []
    for label, catalog in [('control', control), ('korean', args.catalog)]:
        d = work/label
        (d/'cwd/shots').mkdir(parents=True)
        shutil.copytree(fixture, d/'profile')
        command = [args.host.resolve(), '--game', args.ax.resolve(), '--game-root', ROOT/'elonaplus2.335',
                   '--packed-dir', ROOT/'work/extracted/2.335', '--user-root', d/'profile',
                   '--work-dir', d/'cwd', '--script', work/'input.script', '--headless', '--deterministic',
                   '--translation', catalog.resolve(), '--report', d/'report.json', '--translation-misses', d/'miss.tsv']
        with (d/'host.log').open('wb') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120, check=True)
        report = json.loads((d/'report.json').read_text())
        if report['hsp_error'] or report['first_unsupported'] or report['observed_hsp_error_count']:
            raise RuntimeError(f'host error in {d}')
        reports.append(report['frames'])
        save = d/'profile/save'
        if struct.unpack_from('<i', payload(save/'sav_Ari/gdata.s1'), 4*475)[0] != 1:
            raise ValueError(f'quest 225 did not advance in {d}')
        hashes.append({p.relative_to(save).as_posix():digest(payload(p)) for p in save.rglob('*') if p.is_file()})
        pictures.append((d/'cwd/shots/npc.bmp').read_bytes())
    # Compare the rendered paragraph only. Changing the dialogue dictionary
    # must leave the portrait, speaker, choices, HUD and background intact.
    def mask_paragraph(bmp):
        if struct.unpack_from('<iiHHI', bmp, 18) != (800, 600, 1, 24, 0):
            raise ValueError('expected the 800x600 24-bit gameplay bitmap')
        offset = struct.unpack_from('<I', bmp, 10)[0]
        masked = bytearray(bmp)
        for y in range(145, 241):
            start = offset + ((599-y)*800+245)*3
            end = start + (670-245)*3
            masked[start:end] = bytes(end-start)
        return masked

    summary = {'frames': reports, 'save_files': len(hashes[0]), 'quest_225_accepted': True,
               'frames_equal': reports[0] == reports[1], 'save_payloads_equal': hashes[0] == hashes[1],
               'removing_dialogue_keys_changes_rendered_window': pictures[0] != pictures[1],
               'only_paragraph_pixels_changed': mask_paragraph(pictures[0]) == mask_paragraph(pictures[1])}
    (work/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(work, summary)
    return 0 if all(summary.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
