"""Identify authored two-literal choices at the last unambiguous 2.335 draw or log call."""
import hashlib
import json
from pathlib import Path
import re

from extract_localization import PAIR, unquote

ROOT = Path(__file__).resolve().parents[1]
RESOURCE = ROOT / 'src/localization/ko-KR/original-fixed-adventure.json'
DRAW = re.compile(r'(?:mes|bmes|display_topic|cs_list) (lang\(.+)')


def arguments(text):
    """Split a HSP argument list without evaluating dynamic siblings or repeating RNG."""
    start = depth = 0
    quoted = escaped = False
    for i, char in enumerate(text):
        if escaped:
            escaped = False
        elif quoted and char == '\\':
            escaped = True
        elif char == '"':
            quoted = not quoted
        elif not quoted:
            if char == '(':
                depth += 1
            elif char == ')':
                depth -= 1
            elif char == ',' and depth == 0:
                yield text[start:i].strip()
                start = i + 1
    yield text[start:].strip()


def pair_id(argument):
    match = PAIR.fullmatch(argument)
    if match:
        pair = unquote(match[1]) + '\0' + unquote(match[2])
        return hashlib.sha256(pair.encode()).hexdigest()[:20]
    return None


def fixed_inserts(source, reserved):
    rows = json.loads(RESOURCE.read_text(encoding='utf-8'))['entries']
    ids = {row['id'] for row in rows if row.get('source', '').startswith('@fixed-id/')}
    offset = 0
    for raw in source.splitlines(keepends=True):
        start = offset
        offset += len(raw)
        line = raw.lstrip(b'\t')
        if b'lang(' not in line or not line.startswith((b'txt_select ', b'mes lang(', b'bmes lang(', b'display_topic lang(', b'cs_list lang(')):
            continue
        command = line.decode('cp932').strip()
        indent = raw[:len(raw) - len(line)]
        if command.startswith('txt_select '):
            slots = [(n, pair_id(arg)) for n, arg in enumerate(arguments(command[11:]))]
            known = [(n, pid) for n, pid in slots if pid in ids]
            if not known:
                continue
            setup = ''.join(f'if ( txtc == {n} ) : native_log_identity = "@fixed-id/{pid}"\r\n' for n, pid in known)
            yield start, b''.join(indent + s for s in setup.encode().splitlines(keepends=True)), 'fixed_log_choice'
            yield offset, indent + b'native_log_identity = ""\r\n', 'fixed_log_end'
        elif start not in reserved and offset not in reserved:
            match = DRAW.fullmatch(command)
            if not match:
                continue
            arg = next(arguments(match[1]))
            pid = pair_id(arg)
            if pid in ids:
                begin = f'native_ko_menu 20, "@fixed-id/{pid}\\t" + {arg}\r\n'.encode('cp932')
                yield start, indent + begin, 'fixed_literal_begin'
                yield offset, indent + b'native_ko_menu 0, ""\r\n', 'fixed_literal_end'
