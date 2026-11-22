param([Parameter(Mandatory = $true)][string]$RunName)
$ErrorActionPreference = 'Stop'
if ($RunName -notmatch '^gui-native-[A-Za-z0-9_-]+$') { throw 'bad run name' }
$base = Join-Path $env:USERPROFILE ('ElonaPlusNative-2.335\reference-build\' + $RunName)
$inp = Join-Path $base 'input'
$run = Join-Path $base 'localization-check'
if (Test-Path -LiteralPath $run) { throw 'localization evidence already exists' }
$null = New-Item -ItemType Directory -Path $run
$hostExe = Join-Path $base 'build\elona_gui_host.exe'
$compiler = Join-Path $base 'build\elona_hspcmp.exe'
$common = Join-Path $base 'src\.cache\upstream\OpenHSP\common'
function Hash([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }
function WriteJson([string]$p, $o) { [IO.File]::WriteAllText($p, ($o | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false)) }
function RunHost([string[]]$HostArgs, [string]$Dir) {
    $psi = New-Object Diagnostics.ProcessStartInfo $hostExe
    $psi.Arguments = ($HostArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $p = [Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    try { if (-not $p.WaitForExit(60000)) { $p.Kill(); $p.WaitForExit(); throw 'owned host timeout' } }
    finally {
        [IO.File]::WriteAllText((Join-Path $Dir 'stdout.log'), $o.Result)
        [IO.File]::WriteAllText((Join-Path $Dir 'stderr.log'), $e.Result)
    }
    return $p.ExitCode
}
$font = Join-Path $env:WINDIR 'Fonts\malgun.ttf'
if (-not (Test-Path -LiteralPath $font)) { throw 'installed Malgun Gothic font missing' }
$report = [ordered]@{ hostSha256 = (Hash $hostExe); catalogSha256 = (Hash (Join-Path $inp 'ko-runtime.tsv')); font = [ordered]@{ path = $font; sha256 = (Hash $font); redistributed = $false }; cases = @() }
foreach ($name in @('ko-display','ko-modal','ko-help')) {
    $ErrorActionPreference = 'Continue'
    & $compiler -d ('--compath=' + $common + '\') ('-o' + (Join-Path $run ($name + '.ax'))) (Join-Path $inp ($name + '.hsp')) *> (Join-Path $run ($name + '.compile.log'))
    $status = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($status -ne 0) { throw ('fixture compile failed: ' + $name) }
}
foreach ($name in @('original','korean')) {
    $d = Join-Path $run $name; $null = New-Item -ItemType Directory -Path $d
    $a = @('--trusted-fixture', (Join-Path $run 'ko-display.ax'), '--work-dir', $d, '--deterministic', '--headless', '--script', (Join-Path $inp 'ko-display.script'), '--report', (Join-Path $d 'report.json'), '--screenshot', (Join-Path $d 'screen.bmp'), '--trace-commands')
    if ($name -eq 'korean') { $a += @('--translation', (Join-Path $inp 'ko-runtime.tsv')) }
    $report.cases += [ordered]@{ name = $name; exit = (RunHost $a $d) }
}
# Same host contract fixtures as macOS, still entirely private/headless.
foreach ($name in @('modal','help-reference','help-native')) {
    $d = Join-Path $run $name; $null = New-Item -ItemType Directory -Path $d
    $ax = $(if ($name -eq 'modal') { 'ko-modal.ax' } else { 'ko-help.ax' })
    $script = $(if ($name -eq 'modal') { 'ko-modal.script' } else { 'ko-help.script' })
    $a = @('--trusted-fixture', (Join-Path $run $ax), '--work-dir', $d, '--deterministic', '--headless', '--script', (Join-Path $inp $script), '--report', (Join-Path $d 'report.json'), '--trace-commands')
    if ($name -eq 'help-native') { $a += '--auxiliary-close-hide' }
    $report.cases += [ordered]@{ name = $name; exit = (RunHost $a $d) }
}
# Real session-1 window, owned PID only. Script waits in stop for ~4s so the
# existing probe captures Hangul and the unmodified English edit control.
$window = Join-Path $run 'window'; $null = New-Item -ItemType Directory -Path $window
$line = @('korean-window', $window, ('@' + (Join-Path $window 'capture-ready.txt')), '15000', $hostExe, '--trusted-fixture', (Join-Path $run 'ko-display.ax'), '--work-dir', $window, '--script-realtime', '--script', (Join-Path $inp 'ko-display.script'), '--translation', (Join-Path $inp 'ko-runtime.tsv'), '--report', (Join-Path $window 'report.json'), '--screenshot', (Join-Path $window 'screen.bmp'), '--window-capture', (Join-Path $window 'window.bmp'), '--trace-commands')
$plan = Join-Path $run 'window-plan.tsv'; [IO.File]::WriteAllText($plan, ($line -join "`t"), [Text.UTF8Encoding]::new($false))
$results = Join-Path $run 'window-results'
$probe = Join-Path $run 'WindowsGuiProbe.exe'
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $csc /nologo /target:exe /optimize+ /reference:System.Drawing.dll ('/out:' + $probe) (Join-Path $inp 'ko-WindowsGuiProbe.cs') *> (Join-Path $run 'probe.build.log')
if ($LASTEXITCODE -ne 0) { throw 'localization probe build failed' }
$task = 'ElonaPlusNativeLocalization-' + $RunName
$report.session = [ordered]@{}
try {
    $action = New-ScheduledTaskAction -Execute $probe -Argument ('"' + $plan + '" "' + $results + '"') -WorkingDirectory $run
    $principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
    $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Seconds 60) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
    $null = Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings
    Start-ScheduledTask -TaskName $task
    $deadline = (Get-Date).AddSeconds(60)
    while ((Get-Date) -lt $deadline -and -not (Test-Path -LiteralPath (Join-Path $results 'done.marker'))) { Start-Sleep -Milliseconds 500 }
    $report.session.probeCompleted = Test-Path -LiteralPath (Join-Path $results 'done.marker')
}
finally {
    try { Stop-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue } catch {}
    try { Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction SilentlyContinue } catch {}
    $report.session.taskRemoved = -not [bool](Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue)
    $owned = @(Get-CimInstance Win32_Process | Where-Object { ($_.ExecutablePath -eq $hostExe -or $_.ExecutablePath -eq $probe) -and $_.CommandLine -and $_.CommandLine.Contains($run) })
    $report.session.leftoverBeforeCleanup = $owned.Count
    foreach ($p in $owned) { Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue }
    $report.session.leftoverAfterCleanup = @(Get-CimInstance Win32_Process | Where-Object { ($_.ExecutablePath -eq $hostExe -or $_.ExecutablePath -eq $probe) -and $_.CommandLine -and $_.CommandLine.Contains($run) }).Count
}
WriteJson (Join-Path $run 'summary.json') $report
$report | ConvertTo-Json -Depth 12
