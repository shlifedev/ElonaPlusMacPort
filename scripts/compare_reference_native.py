#!/usr/bin/env python3
"""Run reviewed HSP3.4 fixtures after header conversion and compare real output.

Only use project-generated reference fixtures. The native host is not a sandbox.
No game objects or DLL-dependent fixtures are executed by this command.
"""
import argparse
import json
from pathlib import Path
import platform
import re
import subprocess

from elona_extract import digest, inspect_ax
from normalize_legacy_ax import normalize


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    reference = args.reference.resolve()
    work = args.output.resolve()
    original_game = (Path(__file__).resolve().parents[1] / 'elonaplus2.335').resolve()
    if work == original_game or original_game in work.parents or work in original_game.parents:
        raise ValueError('comparison output must be separate from the original game directory')
    if args.output.is_symlink():
        raise ValueError('comparison output directory must not be a symlink')
    if work == reference or reference in work.parents:
        raise ValueError('comparison outputs must be separate from reference fixtures')
    report_bytes = (reference / 'report.json').read_bytes()
    original = json.loads(report_bytes)
    if original['gameExecuted'] is not False or original['runtimeArchitecture'] != 'x86':
        raise ValueError('expected an x86 fixture-only reference report')
    work.mkdir(parents=True, exist_ok=True)
    # Outputs are a dedicated disposable report directory. Do not follow stale
    # links from a previous invocation into original data or another location.
    for path in work.iterdir():
        if path.is_symlink() or not path.is_file():
            raise ValueError('comparison output contains a link or directory')
    results, excluded = [], []
    names = set()
    for row in original['fixtures']:
        name = row['name']
        if not re.fullmatch(r'[a-z][a-z0-9_]{0,63}', name):
            raise ValueError('invalid fixture name')
        if name in names:
            raise ValueError('duplicate fixture name')
        names.add(name)
        if row.get('pluginSha256'):
            excluded.append({'case': name, 'reason': 'native DLL adapter is not implemented'})
            continue
        if row['compileExit'] != 0 or row['runExit'] != 0 or row['stderrHex']:
            raise ValueError(f'unsuccessful reference run: {name}')
        source = reference / name / (name + '.ax')
        data = source.read_bytes()
        if digest(data) != row['axSha256']:
            raise ValueError(f'reference AX hash mismatch: {name}')
        inventory = inspect_ax(data)
        if inventory['library_count'] or inventory['external_function_count']:
            raise ValueError(f'DLL-dependent fixture was not marked: {name}')
        converted, conversion = normalize(data)
        destination = work / (name + '.ax')
        if destination.is_symlink() or (destination.exists() and destination.read_bytes() != converted):
            raise ValueError(f'existing AX output differs: {name}')
        destination.write_bytes(converted)
        expected = bytes.fromhex(row['stdoutHex']).replace(b'\r\n', b'\n')
        run = subprocess.run([str(args.runtime.resolve()), '--trusted-fixture', str(destination)],
                             cwd=work, capture_output=True, timeout=15)
        actual = run.stdout.replace(b'\r\n', b'\n')
        (work / (name + '.stdout')).write_bytes(run.stdout)
        (work / (name + '.stderr')).write_bytes(run.stderr)
        results.append({'case': name, 'passed': run.returncode == row['runExit'] and actual == expected and not run.stderr,
                        'exit': run.returncode, 'expected_hex': expected.hex(), 'stdout_hex': actual.hex(),
                        'stderr': run.stderr.decode('utf-8', 'replace'), 'ax_sha256': digest(converted),
                        'reference_ax_sha256': digest(data), 'conversion': conversion})
        print(('PASS ' if results[-1]['passed'] else 'FAIL ') + name)
    if not results:
        raise ValueError('no non-plugin reference fixtures were found')
    report = {'platform': platform.platform(), 'machine': platform.machine(),
              'runtime_sha256': digest(args.runtime.read_bytes()), 'reference_report_sha256': digest(report_bytes),
              'reference_runtime_sha256': original['runtimeSha256'], 'results': results,
              'excluded': excluded, 'passed': all(row['passed'] for row in results),
              'limitations': ['Reviewed small fixtures only; no game or DLL execution',
                              'Compared to observed Windows x86 output, not guessed expected values',
                              'This does not establish complete opcode or game equivalence']}
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
