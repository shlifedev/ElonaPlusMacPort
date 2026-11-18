#!/usr/bin/env python3
"""Replay eight real-game paths, count untranslated English and compare decompressed saves."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tests/game'))
from run_playability import STAGES, host_checks, sha
from inspect_save_state import digest, payload


def misses(path):
    counts = Counter()
    for line in path.read_text(encoding='utf-8').splitlines():
        if line and not line.startswith('#'):
            source, n = line.rsplit('\t', 1)
            if re.search(r'[A-Za-z]{3}', source):
                counts[source] += int(n)
    return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run', required=True, help='A new directory under work/ko-broad.')
    ap.add_argument('--ax', required=True, type=Path)
    ap.add_argument('--catalog', required=True, type=Path)
    ap.add_argument('--fallback', type=Path)
    ap.add_argument('--reference', type=Path, help='Earlier eight-stage run with misses.tsv and save-hashes.json.')
    ap.add_argument('--host', type=Path, default=ROOT/'build/native-macos-arm64-legacy-cp932-release/elona_gui_host')
    args = ap.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*',args.run):
        ap.error('choose a simple run name')
    run = ROOT/'work/ko-broad'/args.run
    run.mkdir(exist_ok=False, parents=True)
    names = {'newgame':'create','departure':'home','variant':'wizard'}
    total, before = Counter(), Counter()
    passes = True
    stages = {}
    for stage, parent, script, waits, expected, *_ in STAGES:
        d = run/names.get(stage, stage)
        (d/'cwd/shots').mkdir(parents=True)
        if parent:
            shutil.copytree(run/names.get(parent, parent)/'profile',d/'profile')
        else:
            (d/'profile').mkdir()
        cmd = [args.host.resolve(),'--game',args.ax.resolve(),'--game-root',ROOT/'elonaplus2.335',
               '--user-root',d/'profile','--work-dir',d/'cwd','--packed-dir',ROOT/'work/extracted/2.335',
               '--translation',args.catalog.resolve(),'--translation-misses',d/'misses.tsv',
               '--headless','--deterministic','--script',ROOT/'tests/game'/script,'--max-waits',str(waits),
               '--report',d/'report.json']
        if args.fallback:
            cmd += ['--translation-fallback', args.fallback.resolve()]
        with (d/'host.log').open('wb') as log:
            rc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=180).returncode
        report = json.loads((d/'report.json').read_text())
        checks = host_checks(report, rc, expected)
        counts = misses(d/'misses.tsv')
        total.update(counts)
        save = d/'profile/save'
        hashes = {p.relative_to(save).as_posix():digest(payload(p)) for p in save.rglob('*') if p.is_file()}
        (d/'save-hashes.json').write_text(json.dumps(hashes, indent=2)+'\n')
        checks['has_save'] = len(hashes) >= 154
        if args.reference:
            ref = args.reference/d.name
            before.update(misses(ref/'misses.tsv'))
            checks['save_payloads_equal'] = hashes == json.loads((ref/'save-hashes.json').read_text())
            checks['frames_equal'] = report['frames'] == json.loads((ref/'report.json').read_text())['frames']
        passes = passes and all(checks.values())
        stages[stage] = dict(frames=report['frames'], exit_code=rc, checks=checks,
                             save_files=len(hashes), english_miss_strings=len(counts), english_miss_draws=counts.total())
        print(stage,stages[stage],flush=True)
    summary = {'passed':passes,'stages':stages,'unique_english_misses':len(total),'english_miss_draws':total.total(),
               'ax_sha256':sha(args.ax),'host_sha256':sha(args.host),'catalog_sha256':sha(args.catalog),
               'fallback_sha256':sha(args.fallback) if args.fallback else None,
               'scope':'Unique misses include partial words, player names and file names. A draw count includes repaints.'}
    if args.reference:
        summary.update(reference=str(args.reference),before_unique_english_misses=len(before),before_english_miss_draws=before.total())
    (run/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    return 0 if passes else 1


if __name__ == '__main__':
    raise SystemExit(main())
