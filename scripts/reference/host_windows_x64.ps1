# Windows x64 snapshot build of the integrated elona_gui_host (headless, no SDL)
# and the host_regression fixtures/game stage. Note: PowerShell variables are
# case-insensitive, so never name a variable like the -Game switch. Dedicated run directory only.
param([Parameter(Mandatory = $true)][string]$RunName, [switch]$Game, [int]$MaxWaits = 3000, [int]$TimeoutMs = 240000)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if ($RunName -notmatch '^platform-host-[A-Za-z0-9_-]+$') { throw 'bad run name' }
$ws = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335'
$run = Join-Path $ws ('reference-build\' + $RunName)
$src = Join-Path $run 'src'
if (Test-Path -LiteralPath $src) { throw 'already expanded' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $run 'input\host-src.zip'), $src, [Text.Encoding]::UTF8)
function Hash([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }
function TreeHash([string]$root) {
    $rows = @(Get-ChildItem -LiteralPath $root -Recurse -Force | Sort-Object FullName | ForEach-Object {
        $rel = $_.FullName.Substring($root.Length)
        if ($_.PSIsContainer) { $rel + '=<dir>' } else { $rel + '=' + (Hash $_.FullName) } })
    $sha = [Security.Cryptography.SHA256]::Create()
    $bytes = [Text.Encoding]::UTF8.GetBytes(($rows -join "`n"))
    return [ordered]@{ entries = $rows.Count; sha256 = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
$build = Join-Path $run 'build'
$cmd = Join-Path $run 'build.cmd'
Set-Content -LiteralPath $cmd -Encoding Default -Value @(
    '@echo off',
    ('call "{0}" >nul 2>&1' -f (Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat')), 'if errorlevel 1 exit /b 1',
    ('set "PATH={0};{1};%PATH%"' -f (Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'), (Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja')),
    ('cmake -S "{0}" -B "{1}" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DELONA_LEGACY_CP932=ON -DELONA_LEGACY_RNG=ON -DELONA_GUI=OFF -DELONA_TEXT_TTF=OFF -DBUILD_TESTING=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE 2>&1' -f $src, $build),
    'if errorlevel 1 exit /b 1',
    ('cmake --build "{0}" --target elona_gui_host elona_hspcmp 2>&1' -f $build), 'if errorlevel 1 exit /b 1')
$ErrorActionPreference = 'Continue'
& $env:ComSpec /d /c $cmd 2>&1 | Out-File (Join-Path $run 'build.log') -Encoding utf8
$buildExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
$report = [ordered]@{ buildExit = $buildExit; zipSha256 = (Hash (Join-Path $run 'input\host-src.zip')); fixtures = @() }
$hostExe = Join-Path $build 'elona_gui_host.exe'
$compiler = Join-Path $build 'elona_hspcmp.exe'
function Write-Report { $json = $report | ConvertTo-Json -Depth 8; [IO.File]::WriteAllText((Join-Path $run 'report.json'), $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false)); Write-Output $json }
if (-not (Test-Path -LiteralPath $hostExe)) { Write-Report; Get-Content (Join-Path $run 'build.log') -Tail 40; return }
$report.hostSha256 = Hash $hostExe

function Run-Host([string[]]$HostArgs, [string]$LogPrefix, [int]$Timeout) {
    $psi = New-Object Diagnostics.ProcessStartInfo $hostExe
    $psi.Arguments = ($HostArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $p = [Diagnostics.Process]::Start($psi)
    $outTask = $p.StandardOutput.ReadToEndAsync(); $errTask = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($Timeout)) { $p.Kill(); $p.WaitForExit(); $timedOut = $true } else { $timedOut = $false }
    [IO.File]::WriteAllText($LogPrefix + '.stdout.log', $outTask.Result); [IO.File]::WriteAllText($LogPrefix + '.stderr.log', $errTask.Result)
    return [ordered]@{ exit = $p.ExitCode; timedOut = $timedOut; stderrTail = (($errTask.Result -split "`n") | Select-Object -Last 3) -join ' | ' }
}

$exp = Join-Path $src 'host-export'
$manifest = Get-Content -LiteralPath (Join-Path $exp 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$common = Join-Path $src '.cache\upstream\OpenHSP\common'
$compileDir = Join-Path $run 'compile'
$null = New-Item -ItemType Directory -Path $compileDir
foreach ($variant in @('nonascii', 'ascii')) {
    $vroot = Join-Path $run ('fixtures-' + $variant)
    if ($variant -eq 'nonascii') { $gName = $manifest.game_dir_name; $uName = $manifest.user_dir_name; $wName = $manifest.work_dir_name; $aName = $manifest.ax_dir_name }
    else { $gName = 'game-root'; $uName = 'user-data'; $wName = 'work-cwd'; $aName = 'fixture-ax' }
    $gameRoot = Join-Path $vroot $gName
    $null = New-Item -ItemType Directory -Path $vroot
    Copy-Item -LiteralPath (Join-Path $exp 'game') -Destination $gameRoot -Recurse
    $before = TreeHash $gameRoot
    $axDir = Join-Path $vroot $aName
    $null = New-Item -ItemType Directory -Path $axDir
    $packed = Join-Path $vroot $(if ($variant -eq 'nonascii') { $manifest.packed_dir_name } else { 'packed' })
    Copy-Item -LiteralPath (Join-Path $exp 'packed') -Destination $packed -Recurse
    foreach ($case in $manifest.cases) {
        $hsp = Join-Path $compileDir ($variant + '-' + $case.name + '.hsp')
        Copy-Item -LiteralPath (Join-Path $exp ($case.name + '.hsp')) -Destination $hsp
        $axTmp = [IO.Path]::ChangeExtension($hsp, '.ax')
        $ErrorActionPreference = 'Continue'
        & $compiler -d ('--compath=' + $common + '\') ('-o' + $axTmp) $hsp *> ([IO.Path]::ChangeExtension($hsp, '.compile.log'))
        $ErrorActionPreference = 'Stop'
        $ax = Join-Path $axDir ($case.name + '.ax')
        Copy-Item -LiteralPath $axTmp -Destination $ax
        $caseDir = Join-Path $vroot $case.name
        $user = Join-Path $caseDir $uName; $work = Join-Path $caseDir $wName
        $null = New-Item -ItemType Directory -Path $work
        $r = Run-Host @('--game', $ax, '--game-root', $gameRoot, '--user-root', $user, '--work-dir', $work, '--packed-dir', $packed, '--headless', '--deterministic', '--max-waits', '200', '--timeout-ms', '30000', '--report', (Join-Path $caseDir 'report.json')) (Join-Path $caseDir 'host') 60000
        $problems = @()
        if ($r.exit -ne $case.exit) { $problems += ('exit ' + $r.exit + ' != ' + $case.exit) }
        foreach ($f in $case.files) {
            $p = Join-Path $user ($f.path.Replace('/', '\'))
            if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { $problems += ('missing ' + $f.path) }
            elseif ($f.expected -and ((Hash $p) -ne (Hash (Join-Path $exp $f.expected)))) { $problems += ('bytes differ ' + $f.path) }
        }
        foreach ($d in $case.dirs) { if (-not (Test-Path -LiteralPath (Join-Path $user ($d.Replace('/', '\'))) -PathType Container)) { $problems += ('missing dir ' + $d) } }
        $left = @(Get-ChildItem -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue)
        if ($left.Count) { $problems += ('work dir not empty: ' + (($left | ForEach-Object { $_.Name }) -join ',')) }
        $rep = $null; if (Test-Path -LiteralPath (Join-Path $caseDir 'report.json')) { $rep = Get-Content -LiteralPath (Join-Path $caseDir 'report.json') -Raw -Encoding UTF8 | ConvertFrom-Json }
        $report.fixtures += [ordered]@{ variant = $variant; name = $case.name; kind = $case.kind; exit = $r.exit; problems = $problems; ok = ($problems.Count -eq 0); exitReason = $(if ($rep) { $rep.exit_reason }); hspError = $(if ($rep) { $rep.hsp_error }); stderrTail = $r.stderrTail }
    }
    $after = TreeHash $gameRoot
    $report.('syntheticGameUnchanged_' + $variant) = ($before.sha256 -eq $after.sha256)
}

if ($Game) {
    # Fresh private extraction of the hash-checked original ZIP (CP932 names, like
    # windows_hsp34.ps1). reference-game\run-001 is NOT used: the original EXE was
    # run there on 2026-11-17 and it already contains generated config/user files.
    $zip = Join-Path $ws 'inputs\elonaplus2.335.zip'
    $zipBefore = Hash $zip
    if ($zipBefore -ne '06b3fd7d523bcc75ff5c3805d9ae6d5a3056e179b815f27698e8923b1ea21391') { throw 'original zip hash' }
    $jp = [string][char]0x30B2 + [char]0x30FC + [char]0x30E0      # "ゲーム": exercises Unicode native paths
    $privParent = Join-Path $run ('private ' + $jp)
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $privParent, [Text.Encoding]::GetEncoding(932))
    $private = Join-Path $privParent 'elonaplus2.335'
    if ((Hash (Join-Path $private 'elonaplus.exe')) -ne '7d372ac439788de5288cacf200d46d4352d036aa74ae5715f654aaeab56a1e4e') { throw 'extracted exe hash' }
    $privBefore = TreeHash $private
    $gameDir = Join-Path $run ('game ' + $jp)
    $ax = Join-Path $gameDir ('start ' + $jp + '.ax')
    $user = Join-Path $gameDir ('user ' + $jp); $work = Join-Path $gameDir ('cwd ' + $jp)
    $packedGame = Join-Path $gameDir ('packed ' + $jp)
    $null = New-Item -ItemType Directory -Path $work
    $null = New-Item -ItemType Directory -Path $packedGame
    Copy-Item -LiteralPath (Join-Path $run 'input\start.ax') -Destination $ax
    foreach ($n in @('scene1.hsp', 'scene2.hsp')) { Copy-Item -LiteralPath (Join-Path $run ('input\' + $n)) -Destination (Join-Path $packedGame $n) }
    $r = Run-Host @('--game', $ax, '--game-root', $private, '--user-root', $user, '--work-dir', $work, '--packed-dir', $packedGame, '--headless', '--deterministic', '--max-waits', "$MaxWaits", '--timeout-ms', "$TimeoutMs", '--script', (Join-Path $run 'input\startup_english.script'), '--report', (Join-Path $gameDir 'report.json'), '--trace-commands') (Join-Path $gameDir 'host') ($TimeoutMs + 60000)
    $rep = $null; if (Test-Path -LiteralPath (Join-Path $gameDir 'report.json')) { $rep = Get-Content -LiteralPath (Join-Path $gameDir 'report.json') -Raw -Encoding UTF8 | ConvertFrom-Json }
    $generated = @(); if (Test-Path -LiteralPath $user) { $generated = @(Get-ChildItem -LiteralPath $user -Recurse -File -Force | ForEach-Object { $rel = $_.FullName.Substring($user.Length + 1); $def = Join-Path $private ('original\' + $_.Name); [ordered]@{ path = $rel; bytes = $_.Length; sha256 = (Hash $_.FullName); equalsOriginalDefault = $(if (Test-Path -LiteralPath $def) { (Hash $def) -eq (Hash $_.FullName) } else { $null }) } }) }
    $report.game = [ordered]@{ scope = 'bounded headless deterministic diagnostic run; not new game / save / load'; axSha256 = (Hash $ax); exit = $r.exit; timedOut = $r.timedOut; exitReason = $(if ($rep) { $rep.exit_reason }); frames = $(if ($rep) { $rep.frames }); firstUnsupported = $(if ($rep) { $rep.first_unsupported }); hspError = $(if ($rep) { $rep.hsp_error }); stderrTail = $r.stderrTail; generated = $generated
        originalZipSha256 = $zipBefore; originalZipUnchanged = ((Hash $zip) -eq $zipBefore); privateTree = $privBefore; privateUnchanged = ((TreeHash $private).sha256 -eq $privBefore.sha256)
        workEntries = @(Get-ChildItem -LiteralPath $work -Recurse -Force | ForEach-Object { $_.FullName.Substring($work.Length + 1) }) }
    # ASCII-named copy of the evidence for SFTP retrieval (logs, report, user root).
    $evidence = Join-Path $run 'game'
    $null = New-Item -ItemType Directory -Path $evidence
    foreach ($n in @('report.json', 'host.stdout.log', 'host.stderr.log')) { if (Test-Path -LiteralPath (Join-Path $gameDir $n)) { Copy-Item -LiteralPath (Join-Path $gameDir $n) -Destination $evidence } }
    if (Test-Path -LiteralPath $user) { Copy-Item -LiteralPath $user -Destination (Join-Path $evidence 'user') -Recurse }
    Remove-Item -LiteralPath $privParent -Recurse -Force
}
Write-Report
