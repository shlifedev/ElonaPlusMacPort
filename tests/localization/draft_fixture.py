"""Create a private CP932 fixture for all hash-only Korean draft keys."""
import hashlib
import json
from pathlib import Path


def write_fixture(pairs: Path, fallback: Path, destination: Path) -> int:
    keys = {s.split('\t', 1)[0] for s in fallback.read_text(encoding='utf-8').splitlines() if s}
    originals = {}
    for row in json.loads(pairs.read_text(encoding='utf-8')):
        for source in (row['source_ja'], row['source']):
            key = hashlib.sha256(source.encode('utf-8')).hexdigest()
            if key in keys:
                originals[key] = source
    if not keys or keys != originals.keys():
        raise ValueError('draft fixture keys do not match the private game source')
    source = ['#regcmd 48', '#cmd native_ko_gmes $00', 'screen 0,480,175', 'redraw 0']
    for key in sorted(keys):
        literal = originals[key].replace('\\', '\\\\').replace('"', '\\"')
        source += [f'native_ko_gmes "{literal}",0,0,425,0,0,0,0', 'if stat < 0 : end 99']
    source += ['await 1', 'end 0']
    destination.write_bytes(('\r\n'.join(source) + '\r\n').encode('cp932'))
    return len(keys)
