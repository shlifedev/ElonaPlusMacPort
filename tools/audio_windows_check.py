#!/usr/bin/env python3
"""Build the native music renderer with MSVC x64 on the Windows reference host
and render the original 2.335 music offline (no audio device, no game run).

  python3 tools/audio_windows_check.py --run audio-claude-x64-NN

Uses the dedicated %USERPROFILE%/ElonaPlusNative-2.335/build/<run> folder and
reads the original music from the existing read-only copy in
reference-game/run-001/elonaplus2.335. Results are fetched to work/audio/<run>/.
"""
import argparse
import hashlib
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOST = "home/windows"
WORK = ROOT / "work/audio"
sys.path.insert(0, str(ROOT / "tools"))
from windows_probe import run_powershell  # noqa: E402

COMPARE_TRACKS = ["battle1.mid", "oo39.mp3"]


def powershell(script, timeout=1800):
    result = run_powershell(HOST, script, timeout=timeout)
    out = result.stdout.decode("utf-8", "replace") if isinstance(result.stdout, bytes) else result.stdout
    err = result.stderr.decode("utf-8", "replace") if isinstance(result.stderr, bytes) else result.stderr
    return result.returncode, out, err


def sftp(batch):
    result = subprocess.run(["sftp", "-o", "BatchMode=yes", "-b", "-", HOST], input=batch, text=True,
                            capture_output=True, timeout=1800)
    if result.returncode != 0:
        sys.exit(result.stdout + result.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run", required=True)
    args = parser.parse_args()
    if not args.run.startswith("audio-claude-x64-"):
        sys.exit("run must start with audio-claude-x64-")
    subprocess.run([sys.executable, str(ROOT / "tools/bootstrap_audio.py"), "--verify"], check=True)
    WORK.mkdir(parents=True, exist_ok=True)
    archive = WORK / (args.run + ".zip")
    members = [ROOT / "cmake/ElonaAudio.cmake", ROOT / "tools/bootstrap_audio.py"]
    for base in (ROOT / "src/audio", ROOT / "tests/audio", ROOT / ".cache/audio"):
        members += [p for p in sorted(base.rglob("*")) if p.is_file() and "__pycache__" not in p.parts]
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in members:
            info = zipfile.ZipInfo(path.relative_to(ROOT).as_posix(), date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, path.read_bytes())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    code, out, err = powershell(
        "$d = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\build\\" + args.run + "'\n"
        "if (Test-Path -LiteralPath $d) { throw 'exists' }\n$null = New-Item -ItemType Directory -Path $d\nWrite-Output ready")
    if "ready" not in out:
        sys.exit(out + err)
    sftp(f"put {archive} ElonaPlusNative-2.335/build/{args.run}/src.zip\n")
    renders = "\n".join(
        f"  'build\\elona_music_render.exe \"%GAME%\\sound\\{t}\" out\\{t}.wav --seconds 30 2>&1'," for t in COMPARE_TRACKS)
    script = r"""$ErrorActionPreference = 'Stop'
$d = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\build\RUN'
$zip = Join-Path $d 'src.zip'
if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'DIGEST') { throw 'zip hash mismatch' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($zip, (Join-Path $d 'src'))
$game = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\reference-game\run-001\elonaplus2.335'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$cmakeDir = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaDir = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$src = Join-Path $d 'src'
$null = New-Item -ItemType Directory -Path (Join-Path $src 'out')
$cmd = Join-Path $d 'build.cmd'
$lines = @('@echo off', ('call "{0}" >nul 2>&1' -f $vcvars), 'if errorlevel 1 exit /b 1',
  ('set "PATH={0};{1};%PATH%"' -f $cmakeDir,$ninjaDir), ('set "GAME={0}"' -f $game), ('cd /d "{0}"' -f $src),
  'cmake -S tests/audio -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1', 'if errorlevel 1 exit /b 1',
  'cmake --build build 2>&1', 'if errorlevel 1 exit /b 1',
  'ctest --test-dir build --output-on-failure --no-tests=error 2>&1', 'if errorlevel 1 exit /b 1',
  'build\elona_audio_tests.exe --game-dir "%GAME%" --report out\render-report-windows.json 2>&1', 'if errorlevel 1 exit /b 1',
RENDERS
  'exit /b 0')
Set-Content -LiteralPath $cmd -Value $lines -Encoding Default
& $env:ComSpec /d /c $cmd 2>&1 | Tee-Object -FilePath (Join-Path $d 'build.log')
Write-Output ("exit=" + $LASTEXITCODE)
""".replace("RUN", args.run).replace("DIGEST", digest).replace("RENDERS", renders)
    code, out, err = powershell(script)
    dest = WORK / args.run
    dest.mkdir(parents=True, exist_ok=True)
    (dest / "build.log").write_text(out + "\n" + err)
    print(out[-4000:])
    if "exit=0" not in out:
        sys.exit("Windows build/render failed")
    files = ["render-report-windows.json"] + [t + ".wav" for t in COMPARE_TRACKS]
    sftp("".join(f"get ElonaPlusNative-2.335/build/{args.run}/src/out/{f} {dest / f}\n" for f in files))
    print("fetched", dest)


if __name__ == "__main__":
    main()
