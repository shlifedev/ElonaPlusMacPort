param([string]$RunName = 'fixtures-01', [string]$InputName = 'fixture-input')
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if ($RunName -notmatch '^fixtures-[A-Za-z0-9_-]+$') { throw 'RunName must start fixtures- and use ASCII letters/digits/underscore/hyphen' }
if ($InputName -notmatch '^fixture-input(?:-[A-Za-z0-9_-]+)?$') { throw 'Invalid fixture input directory name' }
$root = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\reference-build'
$sdk = Join-Path $root 'sdk-cp932\hsp34'
$inputs = Join-Path $root $InputName
$run = Join-Path $root $RunName
if (Test-Path -LiteralPath $run) { throw "Run already exists: $run" }

function Assert-Hash([string]$Path, [string]$Expected) {
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $Expected) { throw "Hash mismatch: $Path" }
    return $hash
}
function Invoke-Logged([string]$Program, [string[]]$Arguments, [string]$Log) {
    $ErrorActionPreference = 'Continue'
    $messages = & $Program @Arguments 2>&1
    $code = $LASTEXITCODE
    $messages | Out-File -LiteralPath $Log -Encoding utf8
    return $code
}

$dll = Join-Path $sdk 'hspcmp.dll'
$runtime = Join-Path $sdk 'hsp3cl.exe'
$dllHash = Assert-Hash $dll 'b79bccb9230a26ff9b61def7d6707c13529defedf8c16dcb9ea2f90a2e07b005'
$runtimeHash = Assert-Hash $runtime '29f90169fc3798bd876a14bfac7dd53d846f296b55278162b8d279b5e126308f'
$manifestPath = Join-Path $inputs 'manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$null = New-Item -ItemType Directory -Path $run
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
$compiler = Join-Path $run 'Hsp34Compile.exe'
$runner = Join-Path $run 'Hsp34Run.exe'
foreach ($name in @('Hsp34Compile','Hsp34Run')) {
    $arguments = @('/nologo','/target:exe','/platform:x86','/optimize+',('/out:' + (Join-Path $run ($name + '.exe'))),(Join-Path $root ($name + '.cs')))
    $code = Invoke-Logged $csc $arguments (Join-Path $run ($name + '.build.log'))
    if ($code -ne 0) { throw "Could not compile helper: $name" }
}
$rows = @()
foreach ($fixture in $manifest.fixtures) {
    if ($fixture.name -notmatch '^[a-z0-9_]+$' -or $fixture.source -ne ($fixture.name + '.hsp')) { throw 'Invalid fixture name' }
    $inputSource = Join-Path $inputs $fixture.source
    $sourceHash = Assert-Hash $inputSource $fixture.sourceSha256
    $case = Join-Path $run $fixture.name
    $null = New-Item -ItemType Directory -Path $case
    $source = Join-Path $case $fixture.source
    Copy-Item -LiteralPath $inputSource -Destination $source
    $pluginHash = $null
    if ($fixture.plugin) {
        if ($fixture.plugin -ne 'exrand.dll') { throw 'Only the reviewed exrand fixture may load a plugin' }
        $plugin = Join-Path $inputs 'exrand.dll'
        $pluginHash = Assert-Hash $plugin $manifest.exrandSha256
        Copy-Item -LiteralPath $plugin -Destination (Join-Path $case 'exrand.dll')
        $null = Assert-Hash (Join-Path $case 'exrand.dll') $pluginHash
    }
    $ax = Join-Path $case ($fixture.name + '.ax')
    $compileArguments = @($dll,$source,$ax,(Join-Path $sdk 'common'),(Join-Path $case 'compile'))
    $compileCode = Invoke-Logged $compiler $compileArguments (Join-Path $case 'compiler-helper.log')
    $row = [ordered]@{name=$fixture.name; sourceSha256=$sourceHash; compileExit=$compileCode; pluginSha256=$pluginHash; expectedHex=$fixture.expectedHex}
    if ($compileCode -eq 0) {
        # The executable directory precedes CWD in DLL resolution. Running the
        # SDK's EXE in place can silently load its DLLs instead of game copies.
        # Keep the reviewed runtime beside this case's hash-checked plugins.
        $localRuntime = Join-Path $case 'hsp3cl.exe'
        Copy-Item -LiteralPath $runtime -Destination $localRuntime
        $row.runtimeCopySha256 = Assert-Hash $localRuntime $runtimeHash
        $row.runtimePath = $localRuntime
        $runtimeArguments = @($localRuntime,$ax,(Join-Path $case 'run'))
        $runCode = Invoke-Logged $runner $runtimeArguments (Join-Path $case 'runtime-helper.log')
        $row.runExit = $runCode
        $row.axSha256 = (Get-FileHash -LiteralPath $ax -Algorithm SHA256).Hash.ToLowerInvariant()
        $row.stdoutHex = ([BitConverter]::ToString([IO.File]::ReadAllBytes((Join-Path $case 'run.stdout')))).Replace('-','').ToLowerInvariant()
        $row.stderrHex = ([BitConverter]::ToString([IO.File]::ReadAllBytes((Join-Path $case 'run.stderr')))).Replace('-','').ToLowerInvariant()
    }
    $rows += $row
}
$report = [ordered]@{compilerDllSha256=$dllHash; runtimeSha256=$runtimeHash; runtimeArchitecture='x86'; runtimePlacement='case-local'; inputDirectory=$InputName; manifestSha256=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant(); gameExecuted=$false; fixtures=$rows}
$json = $report | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText((Join-Path $run 'report.json'),$json+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
Write-Output $json
