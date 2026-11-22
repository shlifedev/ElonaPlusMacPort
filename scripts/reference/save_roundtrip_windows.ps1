# Fresh, headless-only native save exchange check. No scheduled tasks/UI.
param([string]$RunName, [string]$ReferenceRun)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if ($RunName -notmatch '^platform-save-[A-Za-z0-9_-]+$' -or $ReferenceRun -notmatch '^gui-native-[A-Za-z0-9_-]+$') { throw 'invalid run names' }
$ws = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335'
if (-not (Test-Path -LiteralPath (Join-Path $ws '.elona-native-workspace'))) { throw 'workspace marker missing' }
$run = Join-Path $ws ('reference-build\' + $RunName)
$ref = Join-Path $ws ('reference-build\' + $ReferenceRun)
$exe = Join-Path $ref 'build\elona_gui_host.exe'
$inp = Join-Path $run 'input'
if (-not (Test-Path -LiteralPath $exe)) { throw 'tested reference host missing' }
if (Test-Path -LiteralPath (Join-Path $run 'result')) { throw 'result exists' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function TreeHash([string]$root) {
    $rows = @(Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName | ForEach-Object { $_.FullName.Substring($root.Length) + '=' + (Hash $_.FullName) })
    $sha = [Security.Cryptography.SHA256]::Create()
    return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($rows -join "`n"))))).Replace('-', '').ToLowerInvariant()
}
$zip = Join-Path $ws 'inputs\elonaplus2.335.zip'
$zipBefore = Hash $zip
if ($zipBefore -ne '06b3fd7d523bcc75ff5c3805d9ae6d5a3056e179b815f27698e8923b1ea21391') { throw 'original zip mismatch' }
$profileZip = Join-Path $inp 'profile.zip'
$profileHash = Hash $profileZip
$result = Join-Path $run 'result'
$null = New-Item -ItemType Directory -Path $result
$privateParent = Join-Path $run 'private'
[IO.Compression.ZipFile]::ExtractToDirectory($zip, $privateParent, [Text.Encoding]::GetEncoding(932))
$gameRoot = Join-Path $privateParent 'elonaplus2.335'
$privateHash = TreeHash $gameRoot
$user = Join-Path $result 'user'
[IO.Compression.ZipFile]::ExtractToDirectory($profileZip, $user, [Text.Encoding]::UTF8)
$cwd = Join-Path $result 'cwd'; $null = New-Item -ItemType Directory -Path $cwd
$argsList = @('--game', (Join-Path $ref 'input\start.ax'), '--game-root', $gameRoot,
    '--user-root', $user, '--work-dir', $cwd, '--packed-dir', (Join-Path $ref 'input'),
    '--headless', '--deterministic', '--script', (Join-Path $inp 'load_inspect_resave.script'),
    '--max-waits', '1400', '--timeout-ms', '120000', '--trace-commands',
    '--report', (Join-Path $result 'report.json'), '--screenshot', (Join-Path $result 'screen0.bmp'))
[IO.File]::WriteAllText((Join-Path $result 'argv.json'), ($argsList | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
$psi = [Diagnostics.ProcessStartInfo]::new($exe)
$psi.Arguments = ($argsList | ForEach-Object { '"' + $_ + '"' }) -join ' '
$psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
$p = [Diagnostics.Process]::Start($psi)
$out = $p.StandardOutput.ReadToEndAsync(); $err = $p.StandardError.ReadToEndAsync()
$timedOut = $false
try {
    if (-not $p.WaitForExit(150000)) { $timedOut = $true; $p.Kill(); $p.WaitForExit() }
}
finally {
    if (-not $p.HasExited) { $p.Kill(); $p.WaitForExit() }
    [IO.File]::WriteAllText((Join-Path $result 'stdout.log'), $out.Result, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $result 'stderr.log'), $err.Result, [Text.UTF8Encoding]::new($false))
}
$evidence = [ordered]@{ run = $RunName; referenceRun = $ReferenceRun; hostSha256 = Hash $exe; pid = $p.Id;
    exit = $p.ExitCode; timedOut = $timedOut; originalZipSha256 = $zipBefore;
    originalZipUnchanged = ((Hash $zip) -eq $zipBefore); privateGameUnchanged = ((TreeHash $gameRoot) -eq $privateHash);
    profileZipUnchanged = ((Hash $profileZip) -eq $profileHash); profileZipSha256 = $profileHash;
    ownedProcessExited = $p.HasExited }
[IO.File]::WriteAllText((Join-Path $result 'run-evidence.json'), ($evidence | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
$evidence | ConvertTo-Json
