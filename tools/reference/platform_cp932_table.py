#!/usr/bin/env python3
"""Dump the authoritative Windows CP932 conversion tables (Win32 API) on home/windows.

Output: work/windows/reference-build/<run>/out/cp932-{decode,encode}.tsv
Runs in the SSH session (no GUI). Uses the .NET Framework csc only to build the dumper.
"""
import argparse
from pathlib import Path

from platform_remote import ROOT, check_run_name, fetch_run, make_remote_dir, ps, put_files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", default="platform-cp932-01")
    args = parser.parse_args()
    run = check_run_name(args.run)
    make_remote_dir(run)
    put_files(run, {"platform_cp932_table.cs": ROOT / "tools/reference/platform_cp932_table.cs"})
    print(ps(f"""
$run = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\reference-build\\{run}'
$csc = Join-Path $env:WINDIR 'Microsoft.NET\\Framework64\\v4.0.30319\\csc.exe'
$exe = Join-Path $run 'platform_cp932_table.exe'
& $csc /nologo /target:exe /optimize+ ('/out:' + $exe) (Join-Path $run 'input\\platform_cp932_table.cs') 2>&1 | Out-File (Join-Path $run 'build.log') -Encoding utf8
if ($LASTEXITCODE -ne 0) {{ throw 'build failed' }}
$out = Join-Path $run 'out'
& $exe $out 2>&1 | Tee-Object -FilePath (Join-Path $run 'run.log')
if ($LASTEXITCODE -ne 0) {{ throw "dump failed $LASTEXITCODE" }}
Get-ChildItem $out | ForEach-Object {{ '{{0}} {{1}} {{2}}' -f $_.Name, $_.Length, (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower() }}
(Get-Culture).Name; (Get-WinSystemLocale).Name
""", timeout=300))
    local = fetch_run(run, ROOT / "work/windows/reference-build")
    print(f"fetched {local}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
