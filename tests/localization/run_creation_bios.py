#!/usr/bin/env python3
"""Check all EN/JP creation previews at their actual 425 x 104 paragraph bounds."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser(description=__doc__)
for name in ('compiler', 'host', 'work'):
    ap.add_argument('--'+name, required=True, type=Path)
args = ap.parse_args()
args.work.mkdir(exist_ok=True, parents=True)
work = Path(tempfile.mkdtemp(prefix='creation-', dir=args.work)).resolve()
for folder in ('game','user/user'):
    (work/folder).mkdir(parents=True)
entries = json.loads((root/'src/localization/ko-KR/original-creation-bios.json').read_text(encoding='utf-8'))
if isinstance(entries, dict):
    entries = entries['entries']
source = ['#regcmd 48', '#cmd native_ko_gmes $00', 'screen 0,450,120', 'redraw 0', 'heights=""']
for entry in entries:
    text = entry['source'].replace('\\','\\\\').replace('"','\\"')
    source += [f'native_ko_gmes "{text}",0,0,425,0,0,0,0', 'if stat < 1 : end 97', 'heights += "" + stat + "\\n"']
source += ['notesel heights', 'notesave "user\\\\heights.txt"', 'await 1', 'end 0']
(work/'bios.hsp').write_bytes(('\r\n'.join(source)+'\r\n').encode('cp932'))
subprocess.run([args.compiler.resolve(), '-d','--compath='+str(root/'.cache/upstream/OpenHSP/common')+'/',
                '-o'+str(work/'bios.ax'),work/'bios.hsp'],check=True,capture_output=True)
run = subprocess.run([args.host.resolve(),'--game',work/'bios.ax','--game-root',work/'game','--user-root',work/'user',
                      '--work-dir',work,'--headless','--deterministic','--report',work/'report.json','--translation',
                      root/'src/localization/ko-KR/runtime.tsv'],capture_output=True)
(work/'stderr.log').write_bytes(run.stderr)
report = json.loads((work/'report.json').read_text())
heights = [int(s) for s in (work/'user/user/heights.txt').read_text().splitlines()]
result = {'rows':len(heights), 'available_height':104, 'tallest_height':max(heights), 'overflow':
          [e['source_ref'] for e,h in zip(entries,heights,strict=True) if h>104]}
result['passed'] = (len(entries)==len(heights) and not result['overflow'] and not run.returncode and
                    not report['first_unsupported'] and not report['observed_hsp_error_count'])
(work/'summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(result,ensure_ascii=False))
raise SystemExit(0 if result['passed'] else 1)
