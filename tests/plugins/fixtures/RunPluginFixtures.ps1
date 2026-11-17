param([string]$RunName = 'plugins-claude-01')
# Runs the reviewed plugin fixtures with the ORIGINAL x86 plugin DLL copies in
# the official HSP 3.4 x86 console runtime. Never runs the game.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if ($RunName -notmatch '^plugins-claude-[A-Za-z0-9_-]+$') { throw 'RunName must start plugins-claude- and use ASCII letters/digits/underscore/hyphen' }
$root = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\reference-build'
$sdk = Join-Path $root 'sdk-cp932\hsp34'
$inputs = Join-Path $root 'plugins-claude-input'
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

# Pinned originals (2.335 distribution) and SDK tools.
$pinned = @{
    'hmm.dll' = '5eed0341f3399b833d56402bf4b9c67bae6469e099ee0b802e9ebee428550565';
    'z.hpi' = '92aa68357593af607ae813b978298484003befac59cc5b4d66b31380210c9bee';
    'exrand.dll' = 'fb99c1443797646428c705740ae0a93aacfe9cd76e3341b0e24970fb061a0b23';
    'hspda.dll' = 'a1fcd341def560bd1aa57e63708d487b722a4fc95f2d8edd14e7a5461ea5eb08'
}
$dll = Join-Path $sdk 'hspcmp.dll'
$runtime = Join-Path $sdk 'hsp3cl.exe'
$dllHash = Assert-Hash $dll 'b79bccb9230a26ff9b61def7d6707c13529defedf8c16dcb9ea2f90a2e07b005'
$runtimeHash = Assert-Hash $runtime '29f90169fc3798bd876a14bfac7dd53d846f296b55278162b8d279b5e126308f'
$guiRuntime = Join-Path $sdk 'hsp3.exe'
$guiRuntimeHash = Assert-Hash $guiRuntime '2d55f188b65e67172adc7f24f4a457bb40fb846c971c1279a3bac5126b426ecf'
$manifestPath = Join-Path $inputs 'manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach ($name in $pinned.Keys) { $null = Assert-Hash (Join-Path $inputs ('dll\' + $name)) $pinned[$name] }

$null = New-Item -ItemType Directory -Path $run
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
# Shared helpers are reused read-only from reference-build (their contract is unchanged).
foreach ($pair in @(@('Hsp34Compile', (Join-Path $root 'Hsp34Compile.cs')), @('Hsp34Run', (Join-Path $root 'Hsp34Run.cs')), @('LstrcmpDump', (Join-Path $inputs 'LstrcmpDump.cs')), @('XnoteHarness', (Join-Path $inputs 'XnoteHarness.cs')))) {
    $arguments = @('/nologo','/target:exe','/platform:x86','/optimize+',('/out:' + (Join-Path $run ($pair[0] + '.exe'))),$pair[1])
    $code = Invoke-Logged $csc $arguments (Join-Path $run ($pair[0] + '.build.log'))
    if ($code -ne 0) { throw "Could not compile helper: $($pair[0])" }
}
$compiler = Join-Path $run 'Hsp34Compile.exe'
$runner = Join-Path $run 'Hsp34Run.exe'

$rows = @()
foreach ($fixture in $manifest.fixtures) {
    if ($fixture.name -notmatch '^[a-z0-9_]+$' -or $fixture.source -ne ($fixture.name + '.hsp')) { throw 'Invalid fixture name' }
    $inputSource = Join-Path $inputs ('src\' + $fixture.source)
    $sourceHash = Assert-Hash $inputSource $fixture.sourceSha256
    $case = Join-Path $run $fixture.name
    $null = New-Item -ItemType Directory -Path $case
    $source = Join-Path $case $fixture.source
    Copy-Item -LiteralPath $inputSource -Destination $source
    foreach ($plugin in $fixture.plugins) {
        if (-not $pinned.ContainsKey($plugin)) { throw "Plugin not allowed: $plugin" }
        Copy-Item -LiteralPath (Join-Path $inputs ('dll\' + $plugin)) -Destination (Join-Path $case $plugin)
    }
    foreach ($data in $fixture.data) {
        if ($data.name -notmatch '^[A-Za-z0-9_.-]+$') { throw 'Invalid data name' }
        $from = Join-Path $inputs ('data\' + $data.name)
        $null = Assert-Hash $from $data.sha256
        Copy-Item -LiteralPath $from -Destination (Join-Path $case $data.name)
    }
    $ax = Join-Path $case ($fixture.name + '.ax')
    $compileArguments = @($dll,$source,$ax,(Join-Path $sdk 'common'),(Join-Path $case 'compile'))
    $compileCode = Invoke-Logged $compiler $compileArguments (Join-Path $case 'compiler-helper.log')
    $row = [ordered]@{name=$fixture.name; sourceSha256=$sourceHash; compileExit=$compileCode; plugins=$fixture.plugins; expectError=$fixture.expectError; runtime=$fixture.runtime}
    if ($compileCode -eq 0) {
        $selected = $runtime
        if ($fixture.runtime -eq 'gui') { $selected = $guiRuntime } elseif ($fixture.runtime -ne 'cl') { throw 'Unknown runtime' }
        # Run a copy of the runtime from the case folder: the executable's folder
        # is searched before the current directory, and the SDK folder ships its
        # own (newer) hspda.dll that would otherwise shadow the 2.335 original.
        $local = Join-Path $case (Split-Path -Leaf $selected)
        Copy-Item -LiteralPath $selected -Destination $local
        $row.runtimeCopy = Split-Path -Leaf $selected
        $runCode = Invoke-Logged $runner @($local,$ax,(Join-Path $case 'run')) (Join-Path $case 'runtime-helper.log')
        $outFile = Join-Path $case 'out.txt'
        if (Test-Path -LiteralPath $outFile) { $row.outHex = ([BitConverter]::ToString([IO.File]::ReadAllBytes($outFile))).Replace('-','').ToLowerInvariant() }
        $row.runExit = $runCode
        $row.axSha256 = (Get-FileHash -LiteralPath $ax -Algorithm SHA256).Hash.ToLowerInvariant()
        $row.stdoutHex = ([BitConverter]::ToString([IO.File]::ReadAllBytes((Join-Path $case 'run.stdout')))).Replace('-','').ToLowerInvariant()
        # Hash the module path each fixture reports via GetModuleFileNameA (actual load, not just a copied file).
        $texts = @([IO.File]::ReadAllText((Join-Path $case 'run.stdout'), [Text.Encoding]::GetEncoding(932)))
        if (Test-Path -LiteralPath $outFile) { $texts += [IO.File]::ReadAllText($outFile, [Text.Encoding]::GetEncoding(932)) }
        $mods = @()
        foreach ($t in $texts) {
            foreach ($m in [regex]::Matches($t, '(?m)^loaded (\S+) (.+?)\r?$')) {
                $p = $m.Groups[2].Value
                $h = $null
                if (Test-Path -LiteralPath $p) { $h = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }
                $mods += [ordered]@{dll=$m.Groups[1].Value; path=$p; sha256=$h}
            }
        }
        $row.loadedModules = $mods
        $row.stderrHex = ([BitConverter]::ToString([IO.File]::ReadAllBytes((Join-Path $case 'run.stderr')))).Replace('-','').ToLowerInvariant()
    }
    $rows += $row
}
$dumpCode = Invoke-Logged (Join-Path $run 'LstrcmpDump.exe') @((Join-Path $run 'lstrcmpa_user.txt'),(Join-Path $run 'lstrcmpa_0411.txt')) (Join-Path $run 'lstrcmp.log')
$xnoteDll = Join-Path $run 'xnote-hspda.dll'
Copy-Item -LiteralPath (Join-Path $inputs 'dll\hspda.dll') -Destination $xnoteDll
$xnoteCode = Invoke-Logged (Join-Path $run 'XnoteHarness.exe') @($xnoteDll,(Join-Path $run 'xnote.txt')) (Join-Path $run 'xnote.log')
$report = [ordered]@{xnoteHarnessExit=$xnoteCode; compilerDllSha256=$dllHash; runtimeSha256=$runtimeHash; guiRuntimeSha256=$guiRuntimeHash; runtimeArchitecture='x86'; manifestSha256=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant(); pluginSha256=$pinned; gameExecuted=$false; lstrcmpDumpExit=$dumpCode; fixtures=$rows}
$json = $report | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText((Join-Path $run 'report.json'),$json+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
Write-Output ("fixtures=" + $rows.Count + " lstrcmp=" + $dumpCode)
