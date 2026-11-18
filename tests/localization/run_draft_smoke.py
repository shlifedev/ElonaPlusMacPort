#!/usr/bin/env python3
"""Check every shipped draft at the native prose lookup and Korean glyph boundary."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from draft_fixture import write_fixture

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
for name in ('compiler', 'host', 'fallback', 'work'):
    p.add_argument('--'+name, required=True, type=Path)
p.add_argument('--pairs', type=Path, default=root/'work/localization/2.335/static-pairs.json')
a = p.parse_args()
a.work.mkdir(exist_ok=True, parents=True)
work = Path(tempfile.mkdtemp(prefix='draft-', dir=a.work)).resolve()
count = write_fixture(a.pairs, a.fallback, work/'all.hsp')
subprocess.run([a.compiler.resolve(), '-d', '--compath='+str(root/'.cache/upstream/OpenHSP/common')+'/',
                '-o'+str(work/'all.ax'), work/'all.hsp'], check=True, capture_output=True)
for folder in ('game', 'user'):
    (work/folder).mkdir()
(work/'empty.tsv').write_bytes(b'')
run = subprocess.run([a.host.resolve(), '--game', work/'all.ax', '--game-root', work/'game', '--user-root', work/'user',
                      '--work-dir', work, '--headless', '--deterministic', '--translation', work/'empty.tsv',
                      '--translation-fallback', a.fallback.resolve(), '--trace-commands', '--report', work/'report.json'],
                     timeout=240, capture_output=True)
(work/'stderr.log').write_bytes(run.stderr)
report = json.loads((work/'report.json').read_text())
matched = report.get('command_counts', {}).get('native_ko_gmes', 0)
passed = (run.returncode == 0 and report['endcode'] == 0 and not report['first_unsupported'] and
          not report['observed_hsp_error_count'] and matched == count)
result = {'passed': passed, 'fallback_sha256': hashlib.sha256(a.fallback.read_bytes()).hexdigest(),
          'draft_keys': count, 'paragraph_lookups': matched, 'exit': run.returncode}
(work/'result.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result))
raise SystemExit(0 if passed else 1)
