# Builds and tests the platform layer (encoding, GameFileSystem, HSP file-event
# adapter, VM end-to-end) as native Windows x64 in a dedicated run directory.
param([Parameter(Mandatory = $true)][string]$RunName)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if ($RunName -notmatch '^platform-[A-Za-z0-9_-]+$') { throw 'bad run name' }
$run = Join-Path $env:USERPROFILE ('ElonaPlusNative-2.335\reference-build\' + $RunName)
$src = Join-Path $run 'src'
if (Test-Path -LiteralPath $src) { throw 'already expanded' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $run 'input\platform-src.zip'), $src, [Text.Encoding]::UTF8)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (-not $vs) { throw 'no VS x64 tools' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$cmakeDir = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaDir = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$build = Join-Path $run 'build'
$table = Join-Path $src 'cp932-table'
$cmd = Join-Path $run 'build.cmd'
$lines = @(
    '@echo off',
    ('call "{0}" >nul 2>&1' -f $vcvars), 'if errorlevel 1 exit /b 1',
    ('set "PATH={0};{1};%PATH%"' -f $cmakeDir, $ninjaDir),
    ('cmake -S "{0}\tests\platform" -B "{1}" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE -DELONA_CP932_TABLE_DIR="{2}" -DOPENHSP_ROOT="{0}\.cache\upstream\OpenHSP" 2>&1' -f $src, $build, $table),
    'if errorlevel 1 exit /b 1',
    ('cmake --build "{0}" 2>&1' -f $build), 'if errorlevel 1 exit /b 1',
    ('ctest --test-dir "{0}" --output-on-failure --no-tests=error 2>&1' -f $build), 'if errorlevel 1 exit /b 1',
    ('"{0}\test_encoding.exe" "{1}" 2>&1' -f $build, $table), 'if errorlevel 1 exit /b 1',
    ('"{0}\test_game_files.exe" "{0}\fs-direct" 2>&1' -f $build), 'if errorlevel 1 exit /b 1',
    ('"{0}\test_game_files_hsp.exe" "{0}\hsp-direct" 2>&1' -f $build), 'if errorlevel 1 exit /b 1'
)
Set-Content -LiteralPath $cmd -Value $lines -Encoding Default
$ErrorActionPreference = 'Continue'
& $env:ComSpec /d /c $cmd 2>&1 | Out-File (Join-Path $run 'build.log') -Encoding utf8
$buildExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
$report = [ordered]@{ buildAndCtestExit = $buildExit; vs = $vs; e2e = @() }
if (-not (Test-Path -LiteralPath (Join-Path $build 'platform_hsp_file_host.exe'))) {
    $report.allE2EOk = $false
    $report.e2eSkipped = 'host not built'
    $json = $report | ConvertTo-Json -Depth 6
    [IO.File]::WriteAllText((Join-Path $run 'report.json'), $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
    Write-Output $json
    Get-Content -LiteralPath (Join-Path $run 'build.log') -Tail 25
    return
}
# VM end-to-end without Python: same fixtures/expectations exported on the Mac.
$e2e = Join-Path $src 'e2e-export'
$work = Join-Path $run 'e2e'
$manifest = Get-Content -LiteralPath (Join-Path $e2e 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$game = Join-Path $work ('game root ' + [char]0x30B2 + [char]0x30FC + [char]0x30E0)
$null = New-Item -ItemType Directory -Path $work
Copy-Item -LiteralPath (Join-Path $e2e 'game') -Destination $game -Recurse
function TreeHash([string]$root) { (Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName | ForEach-Object { $_.FullName.Substring($root.Length) + '=' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }) -join ';' }
$before = TreeHash $game
$compiler = Join-Path $build 'platform_e2e_hspcmp.exe'
$host_ = Join-Path $build 'platform_hsp_file_host.exe'
$common = Join-Path $src '.cache\upstream\OpenHSP\common'
foreach ($case in $manifest.cases) {
    $hsp = Join-Path $work ($case.name + '.hsp')
    Copy-Item -LiteralPath (Join-Path $e2e ($case.name + '.hsp')) -Destination $hsp
    $ax = Join-Path $work ($case.name + '.ax')
    $ErrorActionPreference = 'Continue'
    & $compiler -d ('--compath=' + $common + '\') ('-o' + $ax) $hsp *> (Join-Path $work ($case.name + '.compile.log'))
    $user = Join-Path $work ('user-' + $case.name)
    $psi = New-Object Diagnostics.ProcessStartInfo $host_
    $psi.Arguments = ('"{0}" "{1}" "{2}" {3} "{4}"' -f $ax, $game, $user, $manifest.packed, (Join-Path $e2e $manifest.packed))
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $p = [Diagnostics.Process]::Start($psi)
    $ms = New-Object IO.MemoryStream
    $outTask = $p.StandardOutput.BaseStream.CopyToAsync($ms)
    $err = $p.StandardError.ReadToEnd()
    $p.WaitForExit(); $outTask.Wait()
    $ErrorActionPreference = 'Stop'
    $out = $ms.ToArray()
    [IO.File]::WriteAllBytes((Join-Path $work ($case.name + '.stdout')), $out)
    [IO.File]::WriteAllText((Join-Path $work ($case.name + '.stderr')), $err)
    $norm = [Text.Encoding]::GetEncoding(28591).GetString($out).Replace("`r`n", "`n").Replace("`r`n", "`n")
    $expected = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes((Join-Path $e2e ($case.name + '.expected.stdout'))))
    $ok = ($p.ExitCode -eq $case.exit) -and ($norm -ceq $expected) -and ($err.Contains($case.stderr_contains))
    $report.e2e += [ordered]@{ name = $case.name; exit = $p.ExitCode; expectedExit = $case.exit; stdoutMatch = ($norm -ceq $expected); stderr = $err.Trim(); ok = $ok }
}
$userOps = Join-Path $work 'user-file_ops'
$files = @()
foreach ($f in $manifest.user_files_file_ops) {
    $actual = Join-Path $userOps ($f.path.Replace('/', '\'))
    $same = (Test-Path -LiteralPath $actual) -and ((Get-FileHash -LiteralPath $actual -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath (Join-Path $e2e $f.expected) -Algorithm SHA256).Hash)
    $files += [ordered]@{ path = $f.path; ok = $same }
}
foreach ($d in $manifest.user_dirs_file_ops) { $files += [ordered]@{ path = $d; ok = (Test-Path -LiteralPath (Join-Path $userOps ($d.Replace('/', '\'))) -PathType Container) } }
$report.userFiles = $files
$report.originalIntact = ((TreeHash $game) -eq $before)
$report.allE2EOk = (@($report.e2e | Where-Object { -not $_.ok }).Count -eq 0) -and (@($files | Where-Object { -not $_.ok }).Count -eq 0) -and $report.originalIntact
$json = $report | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText((Join-Path $run 'report.json'), $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Output $json
Get-Content -LiteralPath (Join-Path $run 'build.log') -Tail 25
