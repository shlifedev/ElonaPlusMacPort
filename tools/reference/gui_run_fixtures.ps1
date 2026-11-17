# Runs GUI fixtures on the official HSP 3.4 x86 GUI runtime (hsp3.exe) in the
# interactive session through a temporary same-user Interactive/Limited scheduled
# task. No password, no elevation, no global settings change. The task is removed
# in `finally`; only processes started by GuiFixtureProbe (by PID) are touched.
param([Parameter(Mandatory = $true)][string]$RunName, [int]$TaskTimeoutSeconds = 240)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if ($RunName -notmatch '^gui-[A-Za-z0-9_-]+$') { throw 'RunName must start with gui-' }
$base = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\reference-build'
$run = Join-Path $base $RunName
$inputs = Join-Path $run 'input'
$sdk = Join-Path $base 'sdk-cp932\hsp34'
if (-not (Test-Path -LiteralPath $inputs)) { throw "missing $inputs" }
if (Test-Path -LiteralPath (Join-Path $run 'cases')) { throw 'run already executed' }

function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Assert-Hash([string]$Path, [string]$Expected) { $h = Hash $Path; if ($h -ne $Expected) { throw "hash mismatch $Path $h" }; $h }

$report = [ordered]@{ runName = $RunName; scope = 'Official HSP 3.4 x86 GUI runtime (hsp3.exe) on standalone synthetic fixtures; no game code or assets.' }
$report.hspcmpSha256 = Assert-Hash (Join-Path $sdk 'hspcmp.dll') 'b79bccb9230a26ff9b61def7d6707c13529defedf8c16dcb9ea2f90a2e07b005'
$runtime = Join-Path $sdk 'hsp3.exe'
$report.hsp3Sha256 = Assert-Hash $runtime '2d55f188b65e67172adc7f24f4a457bb40fb846c971c1279a3bac5126b426ecf'
$report.acp = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage').ACP
$report.interactiveExplorer = @(Get-Process explorer -ErrorAction SilentlyContinue | ForEach-Object { $_.SessionId })

$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
foreach ($name in @('Hsp34Compile', 'GuiFixtureProbe')) {
    $out = Join-Path $run ($name + '.exe')
    $refs = @(); if ($name -eq 'GuiFixtureProbe') { $refs = @('/reference:System.Drawing.dll') }
    & $csc /nologo /target:exe /platform:x86 /optimize+ @refs ('/out:' + $out) (Join-Path $inputs ($name + '.cs')) 2>&1 | Out-File (Join-Path $run ($name + '.build.log')) -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "helper build failed: $name" }
}

$plan = Join-Path $run 'plan.tsv'
$cases = Join-Path $run 'cases'
$null = New-Item -ItemType Directory -Path $cases
$rows = @()
$planLines = @()
foreach ($line in (Get-Content -LiteralPath (Join-Path $inputs 'cases.tsv'))) {
    if (-not $line.Trim()) { continue }
    $f = $line.Split("`t")   # name, mode, source file, sha256, timeout_ms
    if ($f[0] -notmatch '^[a-z0-9_]+$' -or $f[2] -notmatch '^[a-z0-9_]+\.hsp$') { throw "bad case $line" }
    $case = Join-Path $cases $f[0]
    $null = New-Item -ItemType Directory -Path $case
    $src = Join-Path $case $f[2]
    $srcHash = Assert-Hash (Join-Path $inputs $f[2]) $f[3]
    Copy-Item -LiteralPath (Join-Path $inputs $f[2]) -Destination $src
    $ax = Join-Path $case ($f[0] + '.ax')
    $ErrorActionPreference = 'Continue'
    $msg = & (Join-Path $run 'Hsp34Compile.exe') (Join-Path $sdk 'hspcmp.dll') $src $ax (Join-Path $sdk 'common') (Join-Path $case 'compile') 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    $msg | Out-File (Join-Path $case 'compile-helper.log') -Encoding utf8
    $row = [ordered]@{ name = $f[0]; mode = $f[1]; sourceSha256 = $srcHash; compileExit = $code }
    if ($code -eq 0) { $row.axSha256 = Hash $ax; $planLines += ($f[0], $f[1], $runtime, $ax, $case, $f[4]) -join "`t" }
    $rows += $row
}
[IO.File]::WriteAllLines($plan, [string[]]$planLines, [Text.UTF8Encoding]::new($false))
$report.cases = $rows

$results = Join-Path $run 'results'
$taskName = 'ElonaPlusGuiFixture-' + $RunName
$report.taskName = $taskName
try {
    $action = New-ScheduledTaskAction -Execute (Join-Path $run 'GuiFixtureProbe.exe') -Argument ('"' + $plan + '" "' + $results + '"') -WorkingDirectory $run
    $principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
    $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Seconds $TaskTimeoutSeconds) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
    $null = Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings
    Start-ScheduledTask -TaskName $taskName
    $deadline = (Get-Date).AddSeconds($TaskTimeoutSeconds)
    while ((Get-Date) -lt $deadline -and -not (Test-Path -LiteralPath (Join-Path $results 'done.marker'))) { Start-Sleep -Milliseconds 500 }
    $info = Get-ScheduledTaskInfo -TaskName $taskName
    $report.taskLastResult = $info.LastTaskResult
    $report.taskState = (Get-ScheduledTask -TaskName $taskName).State.ToString()
    $report.probeCompleted = Test-Path -LiteralPath (Join-Path $results 'done.marker')
}
finally {
    try { Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue } catch {}
    try { Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue } catch {}
    $report.taskRemoved = -not [bool](Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue)
    # Safety net: only processes whose command line references this run directory.
    $left = @(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -and $_.CommandLine.Contains($run) })
    $report.leftoverProcesses = @($left | ForEach-Object { [ordered]@{ pid = $_.ProcessId; name = $_.Name } })
    foreach ($p in $left) { Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue }
    $report.leftoverAfterCleanup = @(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -and $_.CommandLine.Contains($run) }).Count
}
$json = $report | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText((Join-Path $run 'report.json'), $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Output $json
if (Test-Path -LiteralPath (Join-Path $results 'probe.log')) { Get-Content -LiteralPath (Join-Path $results 'probe.log') }
