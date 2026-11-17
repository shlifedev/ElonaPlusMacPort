param(
    [Parameter(Mandatory=$true)][string]$SourceDirectory,
    [ValidateSet('windows-x64-debug','windows-x64-legacy-cp932-debug')]
    [string]$Preset='windows-x64-debug'
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$SourceDirectory = (Resolve-Path -LiteralPath $SourceDirectory).Path
$workspace = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335'
$snapshotRoot = (Join-Path $workspace 'source-snapshots') + '\'
if (!(Test-Path (Join-Path $workspace '.elona-native-workspace')) -or !$SourceDirectory.StartsWith($snapshotRoot,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Only marked dedicated source snapshots may be built by this script'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path $vswhere)) { throw 'Visual Studio discovery tool not found' }
$vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (!$vs) { throw 'Visual Studio C++ x64 tools not installed' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$cmakeDir = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaDir = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$logDirectory = Join-Path $SourceDirectory 'build\windows-verification'
[void](New-Item -ItemType Directory -Force -Path $logDirectory)
$cmdFile = Join-Path $logDirectory 'build.cmd'
$lines = @(
    '@echo off',
    ('call "{0}" 2>&1' -f $vcvars),
    'if errorlevel 1 exit /b 1',
    ('set "PATH={0};{1};%PATH%"' -f $cmakeDir,$ninjaDir),
    ('cd /d "{0}"' -f $SourceDirectory),
    ('cmake --preset {0} -DBUILD_TESTING=ON -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE 2>&1' -f $Preset),
    'if errorlevel 1 exit /b 1',
    ('cmake --build --preset {0} 2>&1' -f $Preset),
    'if errorlevel 1 exit /b 1',
    ('ctest --test-dir build/native-{0} --output-on-failure --no-tests=error -R native_string_walkers 2>&1' -f $Preset),
    'if errorlevel 1 exit /b 1'
)
Set-Content -LiteralPath $cmdFile -Value $lines -Encoding Default
& $env:ComSpec /d /c $cmdFile 2>&1 | Tee-Object -FilePath (Join-Path $logDirectory 'build.log')
if ($LASTEXITCODE -ne 0) { throw "Windows native build failed with $LASTEXITCODE" }
$runtime = Join-Path $SourceDirectory ('build\native-' + $Preset + '\elona_hsp_probe.exe')
if (!(Test-Path $runtime)) { throw 'Native runtime not produced at expected path' }
$fixtureDirectory = Join-Path $SourceDirectory 'work\windows-fixtures'
$reference = Get-Content -LiteralPath (Join-Path $fixtureDirectory 'results.json') -Raw | ConvertFrom-Json
if (!$reference.passed -or @($reference.results).Count -eq 0) { throw 'A nonempty successful fixture report is required' }
$seen = @{}
$results = @(foreach ($case in $reference.results) {
    if ($case.case -cnotmatch '^[a-z][a-z0-9_]{0,63}$' -or $seen.ContainsKey($case.case) -or !$case.passed) { throw 'Invalid, duplicate or unsuccessful fixture' }
    $seen[$case.case]=$true
    $ax = Join-Path $fixtureDirectory ($case.case + '.ax')
    if (!(Test-Path $ax)) { throw "Missing copied fixture: $ax" }
    $axHash = (Get-FileHash $ax -Algorithm SHA256).Hash.ToLowerInvariant()
    if (($case.PSObject.Properties.Name -contains 'ax_sha256') -and ($axHash -ne $case.ax_sha256)) {
        throw "Copied fixture hash mismatch: $($case.case)"
    }
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $runtime
    $start.Arguments = '--trusted-fixture "' + $ax + '"'
    $start.WorkingDirectory = $fixtureDirectory
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    [void]$process.Start()
    $stdout = New-Object System.IO.MemoryStream
    $stderr = New-Object System.IO.MemoryStream
    $outTask = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
    $errTask = $process.StandardError.BaseStream.CopyToAsync($stderr)
    if (!$process.WaitForExit(15000)) { $process.Kill(); throw "Fixture timed out: $($case.case)" }
    $outTask.Wait(); $errTask.Wait()
    $outBytes = $stdout.ToArray()
    [IO.File]::WriteAllBytes((Join-Path $logDirectory ($case.case + '.stdout')), $outBytes)
    [IO.File]::WriteAllBytes((Join-Path $logDirectory ($case.case + '.stderr')), $stderr.ToArray())
    # Latin-1 is only a byte-preserving carrier for CRLF normalization.
    $latin = [Text.Encoding]::GetEncoding(28591)
    $normal = $latin.GetBytes($latin.GetString($outBytes).Replace("`r`n", "`n"))
    $hex = ([BitConverter]::ToString($normal)).Replace('-','').ToLowerInvariant()
    $errorText = [Text.Encoding]::UTF8.GetString($stderr.ToArray()).Replace("`r`n", "`n")
    $passed = $process.ExitCode -eq $case.exit
    if ($case.PSObject.Properties.Name -contains 'expected_hex') {
        $passed = $passed -and ($hex -eq $case.expected_hex) -and ($errorText -eq $case.stderr.Replace("`r`n", "`n"))
    }
    else { $passed = $passed -and ($errorText -eq $case.stderr.Replace("`r`n", "`n")) }
    [ordered]@{case=$case.case;passed=$passed;exit=$process.ExitCode;stdout_hex=$hex;stderr=$errorText;ax_sha256=$axHash}
    $process.Dispose(); $stdout.Dispose(); $stderr.Dispose()
})
$failed = @($results | Where-Object { !$_.passed })
$report = [ordered]@{
    os=[Environment]::OSVersion.VersionString; architecture=$env:PROCESSOR_ARCHITECTURE
    preset=$Preset; string_walkers_passed=$true
    runtime_sha256=(Get-FileHash $runtime -Algorithm SHA256).Hash.ToLowerInvariant()
    results=$results; passed=($failed.Count -eq 0)
    reference_runtime_sha256=$reference.reference_runtime_sha256
    limitations=@('Copied reviewed fixtures, not the original game','Reference scope is limited to the supplied fixture report','No GUI or gameplay verified')
}
$json = $report | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText((Join-Path $logDirectory 'results.json'),$json,[Text.UTF8Encoding]::new($false))
Write-Output $json
if ($failed.Count -gt 0) { exit 1 }
