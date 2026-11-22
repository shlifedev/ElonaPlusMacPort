#!/usr/bin/env python3
"""Open the stock fusion pot and select five materials on a disposable Vernis save."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from inspect_save_state import payload
from play_game import MANAGED_AX

SCRIPT = '''320 key A
420 key A
740 key T
850 key B
915 screenshot shots/recipes.bmp
990 key A
1050 screenshot shots/materials.bmp
1110 key A
1160 screenshot shots/selected.bmp
1220 key A
1320 key A
1420 key A
1520 key A
1610 screenshot shots/count.bmp
1710 exit 0
'''

# Hard-coded expectations describe one stock recipe and a known starter inventory.
# Render them at the same original draw sites to match actual fonts and colours.
EXPECTED = {
    'recipe': '고물 연성 레시피', 'choose': '1. 레시피 선택',
    'chosen': '1. 고물 연성 레시피를 골랐다', 'stock': '2. 물건 아무거나 추가 (보유: 18)',
    'rate': '성공률: 절대 무리다!', 'time': '필요 시간: 1턴', 'skill': '연금술  1(0)',
    'recipes': '레시피 73개', 'items': '아이템 5개', 'item': '크림 에일 ×2',
    'add': '물건 아무거나 추가', 'selected': '2. 선택한 재료: 크림 에일 ×2',
    'added': '재료를 더했다: 크림 에일 ×2.', 'confirm0': '조합 시작', 'confirm1': '이전 단계로',
    'hint-back': '{1},{2} [쪽 이동]  시프트·이스케이프 [뒤로]',
}
# The host's deterministic game fixture is 800x600. Each crop selects glyphs
# of just one row, excluding cursor art and neighbouring progress indicators.
CHECKS = {
    'recipes': {
        'recipe': (94,132,244,150), 'choose': (421,137,720,152),
        'stock': (421,154,720,170), 'skill': (421,307,541,323),
        'recipes': (135,437,232,453), 'rate': (426,8,642,28), 'time': (426,37,642,56)},
    'materials': {
        'add': (78,76,207,92), 'item': (94,132,245,150),
        'preview': (412,269,695,285), 'chosen': (421,137,720,152),
        'items': (135,437,232,453)},
    'selected': {'selected': (421,154,720,170), 'added': (148,544,550,560)},
    'count': {'confirm0': (344,228,447,245), 'confirm1': (344,249,447,265),
              'hint-back': (410,458,755,479)},
}


def pixels(path, rect):
    data = path.read_bytes()
    if struct.unpack_from('<iiHHI', data, 18) != (800,600,1,24,0):
        raise ValueError('expected the stock 800x600 24-bit display')
    off = struct.unpack_from('<I', data, 10)[0]
    x, y, right, bottom = rect
    return b''.join(data[off+((599-row)*800+x)*3:off+((599-row)*800+right)*3] for row in range(y,bottom))


def save_hashes(slot):
    return {p.name:hashlib.sha256(payload(p)).hexdigest() for p in slot.iterdir() if p.is_file()}


def scoped_samples(args, work):
    # The two legacy English category labels are identical. Only the actual
    # material ID distinguishes the stock barrel from fishing bait.
    cases = [
        (['stock','3','any bait','147','9023','0'], '3.Add any bait(Stock:147)', '3. 낚시 미끼 추가 (보유: 147)'),
        (['stock','3','any bait','147','9024','0'], '3.Add any bait(Stock:147)', '3. 나무통 아무거나 추가 (보유: 147)'),
        (['stock','3','適当なタル','147','9024','0'], '3.適当なタルを加える(所持:147)', '3. 나무통 아무거나 추가 (보유: 147)'),
        (['selected','4','Start {1}','','','-1'], '4.Selected Start {1}', '4. 선택한 재료: Start {1}'),
        (['selected','4','猫','','','-1'], '4.猫を選んだ', '4. 선택한 재료: 猫'),
        (['stock','2','troll corpse','1','204','10251'], '2.Add troll corpse(Stock:1)', '2. 트롤의 시체 추가 (보유: 1)'),
        (['hours','17','12'], 'Time: 17 turns and 12 hours', '필요 시간: 17턴 12시간'),
        (['gold','9999'], 'Add 9999 gold.', '교환할 금화 +9999개.'),
    ]
    d = work/'samples'
    (d/'game').mkdir(parents=True)
    (d/'profile/user').mkdir(parents=True)
    hsp = ['#regcmd 48','#cmd native_ko_menu $07','screen 0,800,600',
           'color 255,255,255 : boxf 0,0,799,599','color 0,0,0','font "MS Gothic",12']
    for i,(args_,visible,_) in enumerate(cases):
        fields = r'\t'.join([*args_,visible])
        hsp += [f'native_ko_menu 28, "{fields}"',f'pos 8,{20*i} : mes "{visible}"',
                'native_ko_menu 0,""', f'pos 408,{20*i} : mes "@sample/{i}"']
    hsp += ['bmpsave "user/samples.bmp"','end 0']
    source = d/'samples.hsp'
    source.write_bytes(('\r\n'.join(hsp)+'\r\n').encode('cp932'))
    ax = d/'samples.ax'
    subprocess.run([args.compiler.resolve(), '--compath='+str(ROOT/'.cache/upstream/OpenHSP/common')+'/',
                    '-o'+str(ax),source],check=True,capture_output=True)
    catalog = d/'samples.tsv'
    catalog.write_bytes(args.catalog.read_bytes()+b'\n'+''.join(
        f'@sample/{i}\t{target}\n' for i,(_,_,target) in enumerate(cases)).encode())
    subprocess.run([args.host.resolve(),'--game',ax,'--game-root',d/'game','--user-root',d/'profile',
                    '--work-dir',d,'--headless','--deterministic','--translation',catalog],
                   check=True,capture_output=True,timeout=30)
    bmp = d/'profile/user/samples.bmp'
    for i, sample in enumerate(cases):
        left = pixels(bmp,(0,20*i,400,20*i+20))
        right = pixels(bmp,(400,20*i,800,20*i+20))
        assert left == right and len(set(left)) > 2, (sample, d)
    return len(cases)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile', type=Path, required=True, help='English profile from world_to_vernis.script')
    ap.add_argument('--host', type=Path, default=ROOT / 'build/native-macos-arm64-legacy-cp932-release/elona_gui_host')
    ap.add_argument('--ax', type=Path, default=ROOT / MANAGED_AX)
    ap.add_argument('--compiler', type=Path, default=ROOT / 'build/native-macos-arm64-legacy-cp932-release/elona_hspcmp')
    ap.add_argument('--catalog', type=Path, default=ROOT / 'src/localization/ko-KR/runtime.tsv')
    ap.add_argument('--work', type=Path, default=ROOT / 'work/ko-recipe')
    args = ap.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='recipe-', dir=args.work)).resolve()
    fixture = work/'fixture'
    shutil.copytree(args.profile, fixture)
    slot = Path('save/sav_Ari')
    bag = fixture/slot/'inv.s1'
    original = bag.read_bytes()
    data = bytearray(payload(bag))
    if struct.unpack_from('<i', data, 11*70*4)[0] or struct.unpack_from('<i', data, (9*70+3)*4)[0] != 31:
        raise ValueError('expected the Vernis replay with slot 11 free and crim ale in slot 9')
    # Original item 789 (start.hsp dbmode 3) is a reusable fusion pot.
    item = [0]*70
    item[:9] = [1,1000,554,789,1,0,0,1000,3]
    item[19] = 15
    struct.pack_into('<70i', data, 11*70*4, *item)
    bag.write_bytes(gzip.compress(data,mtime=0) if original.startswith(b'\x1f\x8b') else data)
    baseline = save_hashes(fixture/slot)
    (work/'input').write_text(SCRIPT)
    catalog = args.catalog.read_text(encoding='utf-8').splitlines()
    prefix = '@recipe/form/'
    expected, control = [], []
    for line in catalog:
        key = line.partition('\t')[0]
        if not key.startswith('@recipe/'):
            control.append(line)
        suffix = key.removeprefix(prefix)
        expected.append(key+'\t'+EXPECTED[suffix] if key.startswith(prefix) and suffix in EXPECTED else line)
    for key in EXPECTED:
        if not any(line.startswith(prefix+key+'\t') for line in catalog):
            raise ValueError('catalog is missing recipe form '+key)
    frames, saves = [], []
    for mode, rows in [('actual',catalog),('expected',expected),('control',control)]:
        d = work/mode
        (d/'shots').mkdir(parents=True)
        shutil.copytree(fixture,d/'profile')
        table = d/'catalog.tsv'
        table.write_text('\n'.join(rows)+'\n',encoding='utf-8')
        cmd = [args.host.resolve(),'--game',args.ax.resolve(),'--game-root',ROOT/'elonaplus2.335',
               '--packed-dir',ROOT/'work/extracted/2.335','--user-root',d/'profile','--work-dir',d,
               '--headless','--deterministic','--script',work/'input','--report',d/'report.json',
               '--translation',table]
        with (d/'host.log').open('wb') as log:
            subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
        report = json.loads((d/'report.json').read_text())
        assert not report['first_unsupported'] and not report['hsp_error'] and not report['observed_hsp_error_count'], d
        saves.append(save_hashes(d/'profile'/slot))
        frames.append(report['frames'])
    assert len(set(frames)) == 1, frames
    assert all(state == saves[0] for state in saves), work
    assert saves[0]['inv.s1'] == baseline['inv.s1'], work
    for screen, checks in CHECKS.items():
        for role, rect in checks.items():
            actual, expected, control = [pixels(work/mode/'shots'/(screen+'.bmp'),rect)
                                        for mode in ('actual','expected','control')]
            assert actual == expected and actual != control, (screen,role,work)
    samples = scoped_samples(args, work)
    print(f'{sum(map(len,CHECKS.values()))} stock recipe UI crops and {samples} scoped samples matched Korean reference; identical save payloads: {work}')


if __name__ == '__main__':
    main()
