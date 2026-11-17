param(
    [string]$Workspace = (Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335'),
    [string]$RunName = 'baseline',
    [ValidateSet('raw','control-flow-fixed','reference-fixed')][string]$SourceVariant = 'raw'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
if ($RunName -notmatch '^[A-Za-z0-9_-]+$') { throw 'RunName must contain only ASCII letters, digits, underscore or hyphen' }
$expectedRoot = [IO.Path]::GetFullPath((Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335')).TrimEnd('\')
$Workspace = [IO.Path]::GetFullPath($Workspace).TrimEnd('\')
if ($Workspace -ne $expectedRoot) { throw 'Only the dedicated user-profile workspace is allowed' }
$referenceRoot = Join-Path $Workspace 'reference-build'
$sdkArchive = Join-Path $Workspace 'inputs\hsp34a.zip'
$source = Join-Path $referenceRoot 'input\start.hsp'
$expectedSourceHash = 'dce3e305f91e377fd669a045d9dce7cc197509d094852e56b67be312aa233725'
if ($SourceVariant -eq 'control-flow-fixed') {
    $source = Join-Path $referenceRoot 'input\start-control-flow-fixed.hsp'
    $expectedSourceHash = 'a9a6e5cdc6f08a664fb581647f06181667b5943e86de57cd0dfef32567823669'
}
if ($SourceVariant -eq 'reference-fixed') {
    $source = Join-Path $referenceRoot 'input\start-reference-fixed.hsp'
    $expectedSourceHash = 'c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55'
}
$helperSource = Join-Path $referenceRoot 'Hsp34Compile.cs'
$sdkParent = Join-Path $referenceRoot 'sdk-cp932'
$sdk = Join-Path $sdkParent 'hsp34'
$run = Join-Path $referenceRoot $RunName

function Assert-Hash([string]$Path, [string]$Expected) {
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) { throw "Hash mismatch: $Path (got $actual)" }
    return $actual
}

$sdkHash = Assert-Hash $sdkArchive '2b841c71bbe4efad0dce1bed88bdd72bfd4ebe5e5671689b4a5833c105b278cf'
$sourceHash = Assert-Hash $source $expectedSourceHash
if (Test-Path -LiteralPath $run) { throw "Run directory already exists; use a new RunName: $run" }
if (-not (Test-Path -LiteralPath $sdkParent)) {
    # This legacy ZIP uses CP932 filenames without a UTF-8 flag. Expand-Archive
    # fails on those entries; .NET's explicit encoding overload preserves them.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($sdkArchive, $sdkParent, [Text.Encoding]::GetEncoding(932))
}
$dll = Join-Path $sdk 'hspcmp.dll'
$dllHash = Assert-Hash $dll 'b79bccb9230a26ff9b61def7d6707c13529defedf8c16dcb9ea2f90a2e07b005'
$definitionHash = Assert-Hash (Join-Path $sdk 'common\hspdef.as') '270e99939a413e266de83c78e23ecf9168e4521b449b2d76c51e215c54ad2366'
$null = New-Item -ItemType Directory -Path $run
$runSource = Join-Path $run 'start.hsp'
Copy-Item -LiteralPath $source -Destination $runSource
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $csc)) { throw 'The x86 .NET Framework C# compiler is unavailable' }
$helper = Join-Path $run 'Hsp34Compile.exe'
$helperBuildLog = Join-Path $run 'helper-build.log'
$compilerArguments = @('/nologo', '/target:exe', '/platform:x86', '/optimize+', ('/out:' + $helper), $helperSource)
& $csc @compilerArguments 2>&1 | Out-File -LiteralPath $helperBuildLog -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw "x86 helper compilation failed; see $helperBuildLog" }
$output = Join-Path $run 'start.ax'
$logPrefix = Join-Path $run 'compile'
$helperArguments = @($dll, $runSource, $output, (Join-Path $sdk 'common'), $logPrefix)
$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$helperMessages = & $helper @helperArguments 2>&1
$compileExit = $LASTEXITCODE
$ErrorActionPreference = $previousErrorAction
$helperMessages | Out-File -LiteralPath (Join-Path $run 'helper.log') -Encoding utf8
$outputInfo = $null
if (Test-Path -LiteralPath $output) {
    $outputInfo = [ordered]@{
        path = $output
        bytes = (Get-Item -LiteralPath $output).Length
        sha256 = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$record = [ordered]@{
    sdkArchiveSha256 = $sdkHash
    compilerDllSha256 = $dllHash
    hspdefSha256 = $definitionHash
    sourceSha256 = $sourceHash
    sourceTransformed = ($SourceVariant -ne 'raw')
    sourceVariant = $SourceVariant
    helperSourceSha256 = (Get-FileHash -LiteralPath $helperSource -Algorithm SHA256).Hash.ToLowerInvariant()
    helperBinarySha256 = (Get-FileHash -LiteralPath $helper -Algorithm SHA256).Hash.ToLowerInvariant()
    helperCompiler = $csc
    helperCompilerArguments = $compilerArguments
    helperArguments = $helperArguments
    compileExitCode = $compileExit
    hscCompArguments = @(0,0,0,0)
    helperArchitecture = 'x86'
    output = $outputInfo
    compilerMessages = (Join-Path $run 'compile.utf8.log')
    gameExecuted = $false
    behaviorVerified = $false
    observation = 'Official Windows HSPCMP 3.4 source-to-AX compilation only. No game or reference runtime executed.'
}
$json = $record | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText((Join-Path $run 'report.json'), $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Output $json
if ($compileExit -ne 0) { exit $compileExit }
