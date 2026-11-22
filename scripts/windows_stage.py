#!/usr/bin/env python3
"""Snapshot the native spike, upload it via SFTP, build and test on Windows.

Does not install software or execute game code. Uses existing VS C++ tools.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile

from windows_probe import run_powershell
from windows_snapshot import native_sources

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='home/windows')
    parser.add_argument('--legacy-cp932', action='store_true')
    parser.add_argument('--fixtures-dir', type=Path, help='Explicit validated fixture directory containing results.json and AX files')
    args = parser.parse_args()
    work = ROOT / 'work/windows'
    work.mkdir(parents=True, exist_ok=True)
    archive = work / 'native-source.zip'
    variant = 'legacy-cp932-debug' if args.legacy_cp932 else 'debug'
    preset = 'windows-x64-' + variant
    fixtures = args.fixtures_dir.resolve() if args.fixtures_dir else ROOT / ('build/native-macos-arm64-' + variant + '/semantic-tests')
    result = json.loads((fixtures / 'results.json').read_text())
    if not result['passed']:
        raise SystemExit('Mac fixture validation must pass first')
    if not result.get('results'):
        raise SystemExit('Refusing an empty fixture report')
    names = set()
    for case in result['results']:
        name = case['case']
        if not re.fullmatch(r'[a-z][a-z0-9_]{0,63}', name) or name in names:
            raise SystemExit('Invalid or duplicate fixture name')
        if not case.get('passed'):
            raise SystemExit('All individual fixtures must pass')
        names.add(name)
    paths = list(native_sources(ROOT))
    for rel in ("tests/native", "tests/graphics", "tests/plugins", "tests/platform", "tests/localization",
                ".cache/upstream/OpenHSP/src/hsp3", ".cache/upstream/OpenHSP/src/hspcmp", ".cache/upstream/OpenHSP/common"):
        paths.extend(p for p in sorted((ROOT / rel).rglob("*"))
                     if p.is_file() and not p.is_symlink() and "__pycache__" not in p.parts)
    paths.extend(ROOT / rel for rel in ("CMakeLists.txt", "CMakePresets.json", "scripts/windows_build.ps1",
                                        ".cache/upstream/OpenHSP/LICENSE", ".cache/downloads/zlib-1.3.1.tar.gz",
                                        "src/localization/ko-KR/runtime.tsv"))
    files = [(path, path.relative_to(ROOT).as_posix()) for path in paths]
    files.append((fixtures/'results.json', 'work/windows-fixtures/results.json'))
    for case in result['results']:
        files.append((fixtures/(case['case']+'.ax'), 'work/windows-fixtures/'+case['case']+'.ax'))
    # Store each file once, with fixed old timestamps. A ZIP otherwise copies
    # the Mac wall clock into Windows and can make Ninja endlessly regenerate
    # its manifest if the two machine clocks differ.
    payloads = {name:path.read_bytes() for path,name in files}
    manifest = {name:hashlib.sha256(data).hexdigest() for name,data in payloads.items()}
    payloads['snapshot-manifest.json'] = json.dumps(manifest,sort_keys=True,indent=2).encode('utf-8')
    manifest_hash = hashlib.sha256(payloads['snapshot-manifest.json']).hexdigest()
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for name,data in sorted(payloads.items()):
            info = zipfile.ZipInfo(name, date_time=(2000,1,1,0,0,0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info,data)
    checksum=hashlib.sha256(archive.read_bytes()).hexdigest()
    tag=checksum[:16]
    remote='ElonaPlusNative-2.335/inputs/native-'+tag+'.zip'
    # SFTP accepts slash-containing SSH aliases; scp treats them as local paths.
    batch='put '+json.dumps(str(archive))+' '+json.dumps(remote)+'\n'
    subprocess.run(['sftp','-o','BatchMode=yes','-o','ConnectTimeout=10','-b','-',args.host],input=batch,text=True,check=True,timeout=90)
    script=r'''
$root=Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335'
if(!(Test-Path (Join-Path $root '.elona-native-workspace'))){throw 'Missing project workspace marker'}
$archive=Join-Path $root 'inputs\native-TAG.zip'
if((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'CHECKSUM'){throw 'Snapshot archive hash mismatch'}
$source=Join-Path $root 'source-snapshots\TAG'
if(!(Test-Path $source)){Expand-Archive -LiteralPath $archive -DestinationPath $source}
$manifest=Join-Path $source 'snapshot-manifest.json'
if((Get-FileHash $manifest -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'MANIFESTHASH'){throw 'Snapshot manifest hash mismatch'}
$entries=Get-Content $manifest -Raw | ConvertFrom-Json
foreach($entry in $entries.PSObject.Properties){
 $path=[IO.Path]::GetFullPath((Join-Path $source $entry.Name))
 if(!$path.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid snapshot member path'}
 if((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Snapshot member is a link'}
 if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value){throw ('Snapshot member hash mismatch: '+$entry.Name)}
}
& (Join-Path $source 'scripts\windows_build.ps1') -SourceDirectory $source -Preset 'PRESET'
'''.replace('TAG',tag).replace('CHECKSUM',checksum).replace('PRESET',preset).replace('MANIFESTHASH',manifest_hash)
    run=run_powershell(args.host,script,timeout=180)
    (work/('build-'+tag+'.stdout')).write_bytes(run.stdout)
    (work/('build-'+tag+'.stderr')).write_bytes(run.stderr)
    print(run.stdout.decode('utf-8',errors='replace'))
    print(run.stderr.decode('utf-8',errors='replace'),file=sys.stderr)
    metadata={'snapshot':tag,'preset':preset,'archive_sha256':checksum,'ssh_target':args.host,'exit':run.returncode,'remote_source':'%USERPROFILE%/ElonaPlusNative-2.335/source-snapshots/'+tag}
    (work/'latest-native-run.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(json.dumps(metadata))
    return run.returncode


if __name__=='__main__':
    raise SystemExit(main())
