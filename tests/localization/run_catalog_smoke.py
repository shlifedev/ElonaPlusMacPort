#!/usr/bin/env python3
"""Render every encodable catalog key to check actual lookup and font coverage.

This is a glyph/encoding smoke check, not a UI layout or translation-quality
claim. All generated source and files stay in a new private test directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser(description=__doc__)
for name in ('compiler', 'host', 'catalog', 'work'):
    ap.add_argument('--' + name, type=Path, required=True)
ap.add_argument('--common', type=Path, default=ROOT / '.cache/upstream/OpenHSP/common')
a = ap.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix='catalog-', dir=a.work)).resolve()

def unescape(text):
    out, i = '', 0
    while i < len(text):
        if text[i] == '\\':
            i += 1
            out += {'t': '\t', '\\': '\\'}[text[i]]
        else:
            out += text[i]
        i += 1
    return out

rows = []
for line in a.catalog.read_text(encoding='utf-8').splitlines():
    if line and not line.startswith('#'):
        rows.append(unescape(line.split('\t')[0]))
source = ['screen 0,640,480', 'font "MS Gothic",14', 'color 0,0,0']
for text in rows:
    text.encode('cp932')  # fail instead of silently skipping untested keys
    literal = text.replace('\\', '\\\\').replace('"', '\\"').replace('\t', '\\t')
    source.append('pos 0,0 : mes "' + literal + '"')
source += ['await 1', 'end 0']
(work / 'all.hsp').write_bytes(('\r\n'.join(source) + '\r\n').encode('cp932'))
compile_run = subprocess.run([str(a.compiler.resolve()), '-d', '--compath=' + str(a.common.resolve()) + '/',
                              '-o' + str(work / 'all.ax'), str(work / 'all.hsp')], capture_output=True)
(work / 'compile.log').write_bytes(compile_run.stdout + compile_run.stderr)
assert compile_run.returncode == 0, work / 'compile.log'
cmd = [str(a.host.resolve()), '--trusted-fixture', str(work / 'all.ax'), '--work-dir', str(work),
       '--headless', '--deterministic', '--translation', str(a.catalog.resolve()),
       '--translation-misses', str(work / 'misses.tsv'), '--trace-commands', '--report', str(work / 'report.json')]
run = subprocess.run(cmd, capture_output=True, timeout=120)
(work / 'stderr.log').write_bytes(run.stderr)
report = json.loads((work / 'report.json').read_text()) if (work / 'report.json').exists() else {}
misses = [line for line in (work / 'misses.tsv').read_text().splitlines() if line and not line.startswith('#')]
passed = (run.returncode == 0 and not report.get('first_unsupported') and not report.get('hsp_error')
          and report.get('command_counts', {}).get('mes') == len(rows) and not misses)
result = {'passed': passed, 'keys_rendered': len(rows), 'misses': misses,
          'catalog_sha256': hashlib.sha256(a.catalog.read_bytes()).hexdigest(),
          'scope': 'Actual key lookup, CP932 roundtrip and glyph coverage; no layout-completeness claim.'}
(work / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
print(json.dumps(result, ensure_ascii=False))
raise SystemExit(0 if passed else 1)
