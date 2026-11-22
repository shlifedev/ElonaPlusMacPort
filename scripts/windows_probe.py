#!/usr/bin/env python3
"""Read-only environment probe for the user-provided Windows SSH target."""

import argparse
import base64
import json
from pathlib import Path
import subprocess
import sys


def run_powershell(host: str, script: str, timeout: int = 60) -> subprocess.CompletedProcess:
    prefix = "$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; [Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false); "
    encoded = base64.b64encode((prefix + script).encode("utf-16le")).decode("ascii")
    return subprocess.run(
        ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", host,
         "powershell.exe -NoLogo -NoProfile -NonInteractive -EncodedCommand " + encoded],
        capture_output=True, timeout=timeout,
    )


PROBE = r"""
$tools = [ordered]@{}
foreach ($name in @('git','cmake','ninja','cl','clang','clang-cl','python','py','cargo','rustc','7z')) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    $tools[$name] = [string[]]@($command | Where-Object { $_ } | ForEach-Object { $_.Source })
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = @()
if (Test-Path $vswhere) {
    $raw = & $vswhere -all -products '*' -format json -utf8
    if ($raw) {
        $visualStudio = @(foreach ($item in ($raw | ConvertFrom-Json)) {
            if ($item.installationPath) {
                $item | Select-Object installationPath, installationVersion, displayName
            }
        })
    }
}
$os = Get-CimInstance Win32_OperatingSystem
$workspace = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335'
[ordered]@{
    os = $os.Caption
    osVersion = $os.Version
    architecture = $env:PROCESSOR_ARCHITECTURE
    powershell = $PSVersionTable.PSVersion.ToString()
    sshProcessSession = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
    explorerSessions = @((Get-Process explorer -ErrorAction SilentlyContinue) | Select-Object Id, SessionId)
    tools = $tools
    visualStudio = $visualStudio
    proposedWorkspace = $workspace
    workspaceExists = Test-Path $workspace
} | ConvertTo-Json -Depth 6
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="home/windows")
    parser.add_argument("--output", type=Path, default=Path("work/windows/environment.json"))
    args = parser.parse_args()
    try:
        result = run_powershell(args.host, PROBE)
        if result.returncode:
            print(result.stderr.decode("utf-8", errors="replace"), file=sys.stderr)
            return result.returncode
        data = json.loads(result.stdout.decode("utf-8-sig"))
        data["sshTarget"] = args.host
        data["observation"] = "Environment inventory only; no game or GUI interaction tested."
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(data, ensure_ascii=False, indent=2))
        return 0
    except (subprocess.TimeoutExpired, OSError, ValueError) as error:
        print(f"Windows probe failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
