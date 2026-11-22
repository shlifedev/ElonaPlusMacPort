#!/usr/bin/env python3
"""Expand the observed 96-byte HSP header without interpreting game code.

This is a narrow file-format conversion, not an ABI bridge or game launcher.
It does not make DLL declarations, pointers, or opcodes portable.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys

from elona_extract import ExtractError, digest, inspect_ax

LEGACY_BYTES = 96
CURRENT_BYTES = 112
DELTA = CURRENT_BYTES - LEGACY_BYTES


def normalize(data: bytes) -> tuple[bytes, dict]:
    info = inspect_ax(data)
    # A runtime option block is not part of the observed ElonaPlus layout.
    if struct.unpack_from('<I', data, 92)[0] != 0:
        raise ExtractError('runtime-name option blocks are outside this converter scope')
    output = bytearray(data[:LEGACY_BYTES] + bytes(DELTA) + data[LEGACY_BYTES:])
    struct.pack_into('<I', output, 12, len(output))
    for offset in [*range(16, 80, 8), 80]:
        value = struct.unpack_from('<I', data, offset)[0]
        if value and value < LEGACY_BYTES:
            raise ExtractError('section pointer points inside the legacy header')
        struct.pack_into('<I', output, offset, value + DELTA if value else 0)
    # New runtime-option/extra-option sections are explicitly absent, never
    # read from the first 16 bytes of the original code segment.
    assert output[96:112] == bytes(16)
    assert output[CURRENT_BYTES:] == data[LEGACY_BYTES:]
    sections = []
    for name, section in info['segments'].items():
        offset, size = section['offset'], section['bytes']
        moved = offset + DELTA if offset else 0
        before = data[offset:offset + size]
        after = output[moved:moved + size]
        if before != after:
            raise ExtractError(f'payload changed in {name}')
        sections.append({'name': name, 'original_offset': offset,
                         'normalized_offset': moved, 'bytes': size,
                         'sha256': digest(before)})
    result = bytes(output)
    report = {
        'schema_version': 1, 'input_sha256': digest(data), 'output_sha256': digest(result),
        'input_bytes': len(data), 'output_bytes': len(result),
        'input_header_bytes': LEGACY_BYTES, 'output_header_bytes': CURRENT_BYTES,
        'payload_unchanged': True, 'segments': sections,
        'limitations': ['Header conversion only; no code execution or opcode validation',
                        'DLL/plugin ABI and 32-bit pointers remain unresolved',
                        'Only observed 0x301 / 96-byte header without runtime options is accepted'],
    }
    return result, report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        output, report = normalize(args.input.read_bytes())
        original = (Path(__file__).resolve().parents[1] / 'elonaplus2.335').resolve()
        destination = args.output.resolve()
        if destination == args.input.resolve() or original in destination.parents or destination == original:
            raise ExtractError('output must be separate from input and original game directory')
        if args.output.is_symlink():
            raise ExtractError('output must not be a symlink')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        if args.output.exists():
            if args.output.read_bytes() != output:
                raise ExtractError('existing output differs; choose a new path')
        else:
            with args.output.open('xb') as stream:
                stream.write(output)
        print(json.dumps(report, indent=2))
        return 0
    except (OSError, ValueError) as error:
        print(f'AX normalization failed: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
