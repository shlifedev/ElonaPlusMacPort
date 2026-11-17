# Runs platform file-command fixtures on the official HSP 3.4 x86 console runtime
# (hsp3cl.exe) in the SSH session. Each case runs in its own empty directory.
param([Parameter(Mandatory = $true)][string]$RunName)
$ErrorActionPreference = 'Stop'
if ($RunName -notmatch '^platform-[A-Za-z0-9_-]+$') { throw 'bad run name' }
$base = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\reference-build'
$run = Join-Path $base $RunName
$inputs = Join-Path $run 'input'
$sdk = Join-Path $base 'sdk-cp932\hsp34'
function Hash([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }
if ((Hash (Join-Path $sdk 'hsp3cl.exe')) -ne '29f90169fc3798bd876a14bfac7dd53d846f296b55278162b8d279b5e126308f') { throw 'hsp3cl hash' }
if ((Hash (Join-Path $sdk 'hspcmp.dll')) -ne 'b79bccb9230a26ff9b61def7d6707c13529defedf8c16dcb9ea2f90a2e07b005') { throw 'hspcmp hash' }
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
foreach ($n in @('Hsp34Compile', 'Hsp34Run')) {
    & $csc /nologo /target:exe /platform:x86 /optimize+ ('/out:' + (Join-Path $run ($n + '.exe'))) (Join-Path $inputs ($n + '.cs')) 2>&1 | Out-File (Join-Path $run ($n + '.build.log')) -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "build $n" }
}
$rows = @()
foreach ($src in (Get-ChildItem -LiteralPath $inputs -Filter '*.hsp' | Sort-Object Name)) {
    $name = [IO.Path]::GetFileNameWithoutExtension($src.Name)
    if ($name -notmatch '^[a-z0-9_]+$') { throw "bad fixture $name" }
    $case = Join-Path $run ('cases\' + $name)
    $null = New-Item -ItemType Directory -Path $case
    $copy = Join-Path $case $src.Name
    Copy-Item -LiteralPath $src.FullName -Destination $copy
    $ax = Join-Path $case ($name + '.ax')
    $ErrorActionPreference = 'Continue'
    & (Join-Path $run 'Hsp34Compile.exe') (Join-Path $sdk 'hspcmp.dll') $copy $ax (Join-Path $sdk 'common') (Join-Path $case 'compile') *> (Join-Path $case 'compile-helper.log')
    $cc = $LASTEXITCODE
    $rc = $null
    if ($cc -eq 0) {
        & (Join-Path $run 'Hsp34Run.exe') (Join-Path $sdk 'hsp3cl.exe') $ax (Join-Path $case 'run') *> (Join-Path $case 'run-helper.log')
        $rc = $LASTEXITCODE
    }
    $ErrorActionPreference = 'Stop'
    $listing = @(Get-ChildItem -LiteralPath $case -Recurse | Where-Object { $_.Name -notmatch '^(compile|run|run-helper|compile-helper)\.' -and $_.Extension -notin @('.ax', '.hsp') } | ForEach-Object { $_.FullName.Substring($case.Length + 1) + $(if ($_.PSIsContainer) { '\' } else { ' ' + $_.Length }) })
    $rows += [ordered]@{ name = $name; sourceSha256 = (Hash $copy); compileExit = $cc; runExit = $rc; caseFiles = $listing }
}
$report = [ordered]@{ runtime = 'hsp3cl.exe 3.4 x86'; runtimeSha256 = (Hash (Join-Path $sdk 'hsp3cl.exe')); acp = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage').ACP; cases = $rows }
$json = $report | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText((Join-Path $run 'report.json'), $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Output $json
