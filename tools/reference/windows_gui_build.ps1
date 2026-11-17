# Native Windows x64 GUI verification of elona_gui_host with private SDL3 /
# SDL3_ttf (official MSVC packages, extracted inside this run folder only).
#  1. build (Ninja/MSVC, ELONA_GUI=ON, ELONA_TEXT_TTF=ON) from a source snapshot
#  2. session 0: headless + TTF fixtures and original AX (deterministic scripts)
#  3. session 1: real windows through a temporary same-user Interactive/Limited
#     scheduled task running WindowsGuiProbe (captures only owned PIDs); the
#     task is removed in `finally` and leftover processes of this run are reported.
# No global install, PATH/registry/environment or system setting change.
# PowerShell variables are case-insensitive: never reuse a parameter name.
param([Parameter(Mandatory = $true)][string]$RunName, [int]$TaskTimeoutSeconds = 420)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if ($RunName -notmatch '^gui-native-[A-Za-z0-9_-]+$') { throw 'bad run name' }
$ws = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335'
$run = Join-Path $ws ('reference-build\' + $RunName)
$inp = Join-Path $run 'input'
if (Test-Path -LiteralPath (Join-Path $run 'src')) { throw 'already expanded' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Hash([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }
function TreeSha([string]$root) {
    $rows = @(Get-ChildItem -LiteralPath $root -Recurse -Force | Sort-Object FullName | ForEach-Object {
        $rel = $_.FullName.Substring($root.Length); if ($_.PSIsContainer) { $rel + '=<dir>' } else { $rel + '=' + (Hash $_.FullName) } })
    $sha = [Security.Cryptography.SHA256]::Create()
    return [ordered]@{ entries = $rows.Count; sha256 = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($rows -join "`n"))))).Replace('-', '').ToLowerInvariant() }
}
$manifest = Get-Content -LiteralPath (Join-Path $inp 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$report = [ordered]@{ runName = $RunName; acp = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage').ACP; deps = @() }
foreach ($d in $manifest.deps) {
    $zip = Join-Path $inp $d.name
    $h = Hash $zip
    if ($h -ne $d.sha256) { throw "dependency hash mismatch $($d.name) $h" }
    $report.deps += [ordered]@{ name = $d.name; sha256 = $h }
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, (Join-Path $run 'deps'))
}
[IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $inp 'snapshot.zip'), (Join-Path $run 'src'), [Text.Encoding]::UTF8)
[IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $inp 'fixtures.zip'), (Join-Path $run 'fixtures'), [Text.Encoding]::UTF8)
$sdl = Join-Path $run ('deps\' + $manifest.sdl3_dir)
$ttf = Join-Path $run ('deps\' + $manifest.sdl3_ttf_dir)
$fontPath = Join-Path $env:WINDIR 'Fonts\msgothic.ttc'
$report.font = [ordered]@{ path = $fontPath; exists = (Test-Path -LiteralPath $fontPath); sha256 = $(if (Test-Path -LiteralPath $fontPath) { Hash $fontPath }); note = 'installed system font, read in place (host default candidate list), not copied or redistributed' }

# ---- 1. build
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
$build = Join-Path $run 'build'
$cmd = Join-Path $run 'build.cmd'
Set-Content -LiteralPath $cmd -Encoding Default -Value @(
    '@echo off',
    ('call "{0}" >nul 2>&1' -f (Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat')), 'if errorlevel 1 exit /b 1',
    ('set "PATH={0};{1};%PATH%"' -f (Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'), (Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja')),
    ('cmake -S "{0}" -B "{1}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DELONA_LEGACY_CP932=ON -DELONA_LEGACY_RNG=ON -DELONA_GUI=ON -DELONA_TEXT_TTF=ON -DBUILD_TESTING=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE "-DCMAKE_PREFIX_PATH={2};{3}" -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF 2>&1' -f (Join-Path $run 'src'), $build, $sdl, $ttf),
    'if errorlevel 1 exit /b 1',
    ('cmake --build "{0}" --target elona_gui_host elona_hspcmp 2>&1' -f $build), 'if errorlevel 1 exit /b 1')
$ErrorActionPreference = 'Continue'
& $env:ComSpec /d /c $cmd 2>&1 | Out-File (Join-Path $run 'build.log') -Encoding utf8
$report.buildExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
$hostExe = Join-Path $build 'elona_gui_host.exe'
$compiler = Join-Path $build 'elona_hspcmp.exe'
function Save-Report { $j = $report | ConvertTo-Json -Depth 8; [IO.File]::WriteAllText((Join-Path $run 'report.json'), $j + [Environment]::NewLine, [Text.UTF8Encoding]::new($false)); Write-Output $j }
if (-not (Test-Path -LiteralPath $hostExe)) { Save-Report; Get-Content (Join-Path $run 'build.log') -Tail 40; return }
Copy-Item -LiteralPath (Join-Path $sdl 'lib\x64\SDL3.dll') -Destination $build
Copy-Item -LiteralPath (Join-Path $ttf 'lib\x64\SDL3_ttf.dll') -Destination $build
$report.hostSha256 = Hash $hostExe
$report.cmakeSdl = @(Select-String -LiteralPath (Join-Path $build 'CMakeCache.txt') -Pattern '^SDL3(_ttf)?_DIR' | ForEach-Object { $_.Line })
$report.backendLine = @(Select-String -LiteralPath (Join-Path $run 'build.log') -Pattern 'GUI window backend' | ForEach-Object { $_.Line })

function Run-Host([string[]]$HostArgs, [string]$Prefix, [int]$Timeout) {
    $psi = New-Object Diagnostics.ProcessStartInfo $hostExe
    $psi.Arguments = ($HostArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $p = [Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    $timedOut = -not $p.WaitForExit($Timeout); if ($timedOut) { $p.Kill(); $p.WaitForExit() }
    [IO.File]::WriteAllText($Prefix + '.stdout.log', $o.Result); [IO.File]::WriteAllText($Prefix + '.stderr.log', $e.Result)
    return [ordered]@{ exit = $p.ExitCode; timedOut = $timedOut }
}

# ---- compile fixtures once
$fx = Join-Path $run 'fixtures'
$common = Join-Path $run 'src\.cache\upstream\OpenHSP\common'
$compiled = @()
foreach ($f in $manifest.fixtures) {
    $hsp = Join-Path $fx ($f.name + '.hsp'); $ax = Join-Path $fx ($f.name + '.ax')
    $ErrorActionPreference = 'Continue'
    & $compiler -d ('--compath=' + $common + '\') ('-o' + $ax) $hsp *> (Join-Path $fx ($f.name + '.compile.log'))
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    $compiled += [ordered]@{ name = $f.name; compileExit = $code; axSha256 = $(if (Test-Path -LiteralPath $ax) { Hash $ax }) }
}
$report.compiled = $compiled

# ---- 2. session 0: headless + TTF
$report.headless = @()
foreach ($f in $manifest.fixtures) {
    $case = Join-Path $run ('headless\' + $f.name); $null = New-Item -ItemType Directory -Path $case
    $a = @('--trusted-fixture', (Join-Path $fx ($f.name + '.ax')), '--work-dir', $case, '--deterministic', '--headless', '--max-waits', '600', '--timeout-ms', '20000', '--report', (Join-Path $case 'report.json'), '--screenshot', (Join-Path $case 'screen0.bmp'), '--trace-commands')
    if ($f.script) { $a += @('--script', (Join-Path $fx ($f.name + '.script'))) }
    $r = Run-Host $a (Join-Path $case 'host') 60000
    $report.headless += [ordered]@{ name = $f.name; exit = $r.exit; timedOut = $r.timedOut }
}
$jp = [string][char]0x30B2 + [char]0x30FC + [char]0x30E0
$zip = Join-Path $ws 'inputs\elonaplus2.335.zip'
$zipBefore = Hash $zip
if ($zipBefore -ne '06b3fd7d523bcc75ff5c3805d9ae6d5a3056e179b815f27698e8923b1ea21391') { throw 'original zip hash' }
$privParent = Join-Path $run ('private ' + $jp)
[IO.Compression.ZipFile]::ExtractToDirectory($zip, $privParent, [Text.Encoding]::GetEncoding(932))
$private = Join-Path $privParent 'elonaplus2.335'
$privBefore = TreeSha $private
$packed = Join-Path $run ('packed ' + $jp); $null = New-Item -ItemType Directory -Path $packed
foreach ($n in @('scene1.hsp', 'scene2.hsp')) { Copy-Item -LiteralPath (Join-Path $inp $n) -Destination $packed }
$gameAx = Join-Path $run ('start ' + $jp + '.ax'); Copy-Item -LiteralPath (Join-Path $inp 'start.ax') -Destination $gameAx
$report.games = @()
foreach ($g in $manifest.games) {
    if ($g.mode -ne 'headless') { continue }
    $gd = Join-Path $run ('game-' + $g.name); $user = Join-Path $gd ('user ' + $jp); $work = Join-Path $gd ('cwd ' + $jp)
    $null = New-Item -ItemType Directory -Path $work
    $a = @('--game', $gameAx, '--game-root', $private, '--user-root', $user, '--work-dir', $work, '--packed-dir', $packed, '--deterministic', '--max-waits', "$($g.maxWaits)", '--timeout-ms', "$($g.timeoutMs)", '--script', (Join-Path $inp $g.script), '--report', (Join-Path $gd 'report.json'), '--screenshot', (Join-Path $gd 'screen0.bmp'), '--trace-commands')
    if ($g.mode -eq 'headless') { $a += '--headless' }
    if ($g.mode -eq 'headless') {
        if ($g.reuseUserFrom) {
            # Same as the Mac run28 procedure: copy the profile written by an earlier
            # run into this run's own user root, then start a new process.
            $srcUser = Join-Path (Join-Path $run ('game-' + $g.reuseUserFrom)) ('user ' + $jp)
            Copy-Item -LiteralPath $srcUser -Destination $user -Recurse
        }
        $r = Run-Host $a (Join-Path $gd 'host') ($g.timeoutMs + 60000)
        $report.games += [ordered]@{ name = $g.name; mode = $g.mode; exit = $r.exit; timedOut = $r.timedOut }
    }
}

# ---- 3. session 1: real windows
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$probe = Join-Path $run 'WindowsGuiProbe.exe'
& $csc /nologo /target:exe /optimize+ /reference:System.Drawing.dll ('/out:' + $probe) (Join-Path $inp 'WindowsGuiProbe.cs') 2>&1 | Out-File (Join-Path $run 'probe.build.log') -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw 'probe build failed' }
$plan = @()
foreach ($f in $manifest.fixtures) {
    $case = Join-Path $run ('window\' + $f.name); $null = New-Item -ItemType Directory -Path $case
    $line = @($f.name, $case, '1500', '30000', $hostExe, '--trusted-fixture', (Join-Path $fx ($f.name + '.ax')), '--work-dir', $case, '--deterministic', '--max-waits', '600', '--timeout-ms', '20000', '--report', (Join-Path $case 'report.json'), '--screenshot', (Join-Path $case 'screen0.bmp'), '--window-capture', (Join-Path $case 'window.bmp'), '--trace-commands')
    if ($f.script) { $line += @('--script', (Join-Path $fx ($f.name + '.script'))) }
    $plan += ($line -join "`t")
}
foreach ($g in $manifest.games) {
    if ($g.mode -ne 'window') { continue }
    $gd = Join-Path $run ('game-' + $g.name); $user = Join-Path $gd ('user ' + $jp); $work = Join-Path $gd ('cwd ' + $jp)
    $null = New-Item -ItemType Directory -Path $work
    $captureTrigger = "$($g.captureDelayMs)"
    if ($g.captureReadyScreenshot) { $captureTrigger = '@' + (Join-Path (Join-Path $work 'shots') $g.captureReadyScreenshot) }
    $line = @($g.name, $work, $captureTrigger, "$($g.timeoutMs + 30000)", $hostExe, '--game', $gameAx, '--game-root', $private, '--user-root', $user, '--work-dir', $work, '--packed-dir', $packed, '--deterministic', '--max-waits', "$($g.maxWaits)", '--timeout-ms', "$($g.timeoutMs)", '--script', (Join-Path $inp $g.script), '--report', (Join-Path $gd 'report.json'), '--screenshot', (Join-Path $gd 'screen0.bmp'), '--window-capture', (Join-Path $gd 'window.bmp'), '--trace-commands')
    $plan += ($line -join "`t")
}
$planFile = Join-Path $run 'window-plan.tsv'
[IO.File]::WriteAllLines($planFile, [string[]]$plan, [Text.UTF8Encoding]::new($false))
$results = Join-Path $run 'window-results'
$taskName = 'ElonaPlusNativeGui-' + $RunName
$report.session = [ordered]@{ taskName = $taskName; interactiveExplorer = @(Get-Process explorer -ErrorAction SilentlyContinue | ForEach-Object { $_.SessionId }) }
try {
    $action = New-ScheduledTaskAction -Execute $probe -Argument ('"' + $planFile + '" "' + $results + '"') -WorkingDirectory $run
    $principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
    $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Seconds $TaskTimeoutSeconds) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
    $null = Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings
    Start-ScheduledTask -TaskName $taskName
    $deadline = (Get-Date).AddSeconds($TaskTimeoutSeconds)
    while ((Get-Date) -lt $deadline -and -not (Test-Path -LiteralPath (Join-Path $results 'done.marker'))) { Start-Sleep -Milliseconds 500 }
    $report.session.probeCompleted = Test-Path -LiteralPath (Join-Path $results 'done.marker')
    $report.session.taskLastResult = (Get-ScheduledTaskInfo -TaskName $taskName).LastTaskResult
}
finally {
    try { Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue } catch {}
    try { Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue } catch {}
    $report.session.taskRemoved = -not [bool](Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue)
    # Own only binaries inside this exact run. Do not match a shell/editor
    # merely because its command line mentions the run directory.
    $left = @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($run + '\', [StringComparison]::OrdinalIgnoreCase) })
    $report.session.leftoverProcesses = @($left | ForEach-Object { [ordered]@{ pid = $_.ProcessId; name = $_.Name } })
    foreach ($p in $left) { Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue }
    $report.session.leftoverAfterCleanup = @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($run + '\', [StringComparison]::OrdinalIgnoreCase) }).Count
}
$report.original = [ordered]@{ zipSha256 = $zipBefore; zipUnchanged = ((Hash $zip) -eq $zipBefore); privateTree = $privBefore; privateUnchanged = ((TreeSha $private).sha256 -eq $privBefore.sha256) }
# ASCII-named evidence copies for SFTP retrieval; the private game copy is removed.
$ev = Join-Path $run 'evidence'; $null = New-Item -ItemType Directory -Path $ev
foreach ($g in $manifest.games) {
    $gd = Join-Path $run ('game-' + $g.name); $dst = Join-Path $ev ('game-' + $g.name); $null = New-Item -ItemType Directory -Path $dst
    foreach ($n in @('report.json', 'screen0.bmp', 'window.bmp', 'host.stdout.log', 'host.stderr.log')) { if (Test-Path -LiteralPath (Join-Path $gd $n)) { Copy-Item -LiteralPath (Join-Path $gd $n) -Destination $dst } }
    $work = Join-Path $gd ('cwd ' + $jp); if (Test-Path -LiteralPath (Join-Path $work 'shots')) { Copy-Item -LiteralPath (Join-Path $work 'shots') -Destination (Join-Path $dst 'shots') -Recurse }
    $user = Join-Path $gd ('user ' + $jp); if (Test-Path -LiteralPath $user) { Copy-Item -LiteralPath $user -Destination (Join-Path $dst 'user') -Recurse }
}
Remove-Item -LiteralPath $privParent -Recurse -Force
Save-Report
