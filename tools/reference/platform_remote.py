#!/usr/bin/env python3
"""Shared SSH/SFTP helpers for the platform/GUI reference runs on home/windows.

Only touches %USERPROFILE%\\ElonaPlusNative-2.335\\reference-build\\{gui-*,platform-*}.
Reuses tools/windows_probe.run_powershell (UTF-16LE -EncodedCommand) unchanged.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from windows_probe import run_powershell  # noqa: E402  (read-only reuse)

HOST = "home/windows"
REMOTE_BASE = "ElonaPlusNative-2.335/reference-build"   # relative to SFTP home (= %USERPROFILE%)
RUN_NAME = re.compile(r"^(gui|platform)-[A-Za-z0-9_-]+$")


def check_run_name(name: str) -> str:
    if not RUN_NAME.match(name):
        raise SystemExit(f"run name must match {RUN_NAME.pattern}: {name}")
    return name


def ps(script: str, timeout: int = 120) -> str:
    result = run_powershell(HOST, script, timeout=timeout)
    out = result.stdout.decode("utf-8", "replace")
    err = result.stderr.decode("utf-8", "replace")
    if result.returncode:
        raise RuntimeError(f"remote PowerShell failed ({result.returncode})\nSTDOUT:\n{out}\nSTDERR:\n{err}")
    return out


def sftp(batch: str, timeout: int = 600) -> None:
    result = subprocess.run(["sftp", "-o", "BatchMode=yes", "-b", "-", HOST], input=batch,
                            text=True, capture_output=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"sftp failed\n{batch}\n{result.stdout}\n{result.stderr}")


def make_remote_dir(run: str) -> None:
    """Create a fresh dedicated run directory; refuse to reuse an existing one."""
    check_run_name(run)
    ps(f"""
$base = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\reference-build'
if (-not (Test-Path -LiteralPath (Join-Path $base 'sdk-cp932\\hsp34\\hsp3.exe'))) {{ throw 'SDK not staged' }}
$run = Join-Path $base '{run}'
if (Test-Path -LiteralPath $run) {{ throw "run exists: $run" }}
$null = New-Item -ItemType Directory -Path $run
$null = New-Item -ItemType Directory -Path (Join-Path $run 'input')
'created ' + $run
""")


def put_files(run: str, files: dict[str, Path]) -> None:
    lines = []
    for remote_name, local in files.items():
        if "/" in remote_name or "\\" in remote_name or not re.match(r"^[A-Za-z0-9_.-]+$", remote_name):
            raise SystemExit(f"bad remote file name {remote_name}")
        lines.append(f'put "{local}" {REMOTE_BASE}/{run}/input/{remote_name}')
    sftp("\n".join(lines) + "\n")


def fetch_run(run: str, local_parent: Path) -> Path:
    check_run_name(run)
    local_parent.mkdir(parents=True, exist_ok=True)
    destination = local_parent / run
    if destination.exists():
        raise SystemExit(f"local destination exists: {destination}")
    sftp(f'lcd "{local_parent}"\nget -R {REMOTE_BASE}/{run}\n', timeout=900)
    return destination
