param([Parameter(Mandatory=$true)][string]$RunName)
$ErrorActionPreference='Stop'
if($RunName -notmatch '^gui-native-[A-Za-z0-9_-]+$'){throw 'bad run name'}
$ws=Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335'
$run=Join-Path $ws ('reference-build\'+$RunName)
$inp=Join-Path $run 'input'; $src=Join-Path $run 'src'; $build=Join-Path $run 'build'; $ev=Join-Path $run 'evidence'
if(Test-Path $src){throw 'source already exists'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function Json([string]$p,$o){[IO.File]::WriteAllText($p,($o|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))}
function Tree([string]$p){
 $rows=@(Get-ChildItem -LiteralPath $p -Recurse -File|Sort-Object FullName|ForEach-Object{$_.FullName.Substring($p.Length)+'='+(Hash $_.FullName)})
 $sha=[Security.Cryptography.SHA256]::Create();([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($rows -join "`n"))))).Replace('-','').ToLowerInvariant()
}
$manifest=Get-Content -LiteralPath (Join-Path $inp 'manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json
if((Hash (Join-Path $inp 'snapshot.zip')) -ne $manifest.snapshot_sha256){throw 'snapshot hash'}
[IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $inp 'snapshot.zip'),$src,[Text.Encoding]::UTF8)
foreach($d in $manifest.deps){
 $z=Join-Path $inp $d.name;if((Hash $z) -ne $d.sha256){throw 'dependency hash'}
 [IO.Compression.ZipFile]::ExtractToDirectory($z,(Join-Path $run 'deps'))
}
$sdl=Join-Path $run 'deps\SDL3-3.4.20';$ttf=Join-Path $run 'deps\SDL3_ttf-3.2.2'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath|Select-Object -First 1)
$cmd=Join-Path $run 'build.cmd'
Set-Content -LiteralPath $cmd -Encoding Default -Value @('@echo off',
 ('call "{0}" >nul 2>&1' -f (Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat')),'if errorlevel 1 exit /b 1',
 ('set "PATH={0};{1};%PATH%"' -f (Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'),(Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja')),
 ('cmake -S "{0}" -B "{1}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DELONA_LEGACY_CP932=ON -DELONA_LEGACY_RNG=ON -DELONA_GUI=ON -DELONA_TEXT_TTF=ON -DBUILD_TESTING=ON -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE "-DCMAKE_PREFIX_PATH={2};{3}" -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF 2>&1' -f $src,$build,$sdl,$ttf),
 'if errorlevel 1 exit /b 1',('cmake --build "{0}" --target elona_gui_host elona_hspcmp elona_catalog_tests 2>&1' -f $build),'if errorlevel 1 exit /b 1')
$null=New-Item -ItemType Directory -Path $ev
$summary=[ordered]@{runs=@();catalogSha256=(Hash (Join-Path $inp 'runtime.tsv'));managedAxSha256=(Hash (Join-Path $inp 'managed.ax'));referenceAxSha256=(Hash (Join-Path $inp 'reference.ax'))}
$hostExe=Join-Path $build 'elona_gui_host.exe';$compiler=Join-Path $build 'elona_hspcmp.exe'
try{
 $ErrorActionPreference='Continue'; & $env:ComSpec /d /c $cmd 2>&1|Out-File (Join-Path $run 'build.log') -Encoding UTF8
 $summary.buildExit=$LASTEXITCODE;$ErrorActionPreference='Stop'
 if($summary.buildExit -ne 0){throw 'native managed build failed'}
 Copy-Item -LiteralPath (Join-Path $sdl 'lib\x64\SDL3.dll') -Destination $build
 Copy-Item -LiteralPath (Join-Path $ttf 'lib\x64\SDL3_ttf.dll') -Destination $build
 $summary.hostSha256=Hash $hostExe
 $font=Join-Path $env:WINDIR 'Fonts\malgun.ttf';$summary.font=[ordered]@{path=$font;sha256=(Hash $font);redistributed=$false}
 $ErrorActionPreference='Continue'; & (Join-Path $build 'elona_catalog_tests.exe') (Join-Path $ev 'catalog-unit') *> (Join-Path $ev 'catalog-unit.log')
 $summary.catalogTestExit=$LASTEXITCODE;$ErrorActionPreference='Stop'
 function RunHost([string]$Name,[string[]]$HostArgs,[int]$Limit=180000){
  $d=Join-Path $ev $Name
  $psi=New-Object Diagnostics.ProcessStartInfo $hostExe
  $psi.Arguments=($HostArgs|ForEach-Object{'"'+$_+'"'}) -join ' '
  $psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
  Json (Join-Path $d 'argv.json') $HostArgs
  $p=[Diagnostics.Process]::Start($psi);$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync()
  $timedOut=-not $p.WaitForExit($Limit);if($timedOut){$p.Kill();$p.WaitForExit()}
  [IO.File]::WriteAllText((Join-Path $d 'stdout.log'),$o.Result);[IO.File]::WriteAllText((Join-Path $d 'stderr.log'),$e.Result)
  $summary.runs += [ordered]@{name=$Name;exit=$p.ExitCode;timedOut=$timedOut}
 }
 $common=Join-Path $src '.cache\upstream\OpenHSP\common'
 $fixtures=@{display=(Join-Path $src 'tests\localization\display.hsp');paragraph=(Join-Path $src 'tests\localization\paragraph.hsp');catalog=(Join-Path $inp 'all-catalog.hsp')}
 $cases=@('original','korean','paragraph','catalog')
 if($manifest.draft_keys){
  $fixtures['drafts']=Join-Path $inp 'drafts.hsp'
  $summary.draftSha256=Hash (Join-Path $inp 'draft-display.tsv')
  $cases+='drafts'
 }
 foreach($n in $fixtures.Keys){
  $ErrorActionPreference='Continue'; & $compiler -d ('--compath='+$common+'\') ('-o'+(Join-Path $ev ($n+'.ax'))) $fixtures[$n] *> (Join-Path $ev ($n+'.compile.log'))
  $rc=$LASTEXITCODE;$ErrorActionPreference='Stop';if($rc -ne 0){throw ('fixture compile '+$n)}
 }
 foreach($n in $cases){
  $name='fixture-'+$n;$d=Join-Path $ev $name;$null=New-Item -ItemType Directory -Path $d
  $ax=$(if($n -eq 'original' -or $n -eq 'korean'){'display'}else{$n})
  $a=@('--work-dir',$d,'--headless','--deterministic','--report',(Join-Path $d 'report.json'),'--screenshot',(Join-Path $d 'screen.bmp'),'--trace-commands')
  if($n -eq 'paragraph' -or $n -eq 'drafts'){
   $null=New-Item -ItemType Directory -Path (Join-Path $d 'game'),(Join-Path $d 'user\user')
   $a+=@('--game',(Join-Path $ev ($ax+'.ax')),'--game-root',(Join-Path $d 'game'),'--user-root',(Join-Path $d 'user'),'--translation',$(if($n -eq 'drafts'){Join-Path $inp 'empty.tsv'}else{Join-Path $inp 'paragraph.tsv'}))
   if($n -eq 'drafts'){$a+=@('--translation-fallback',(Join-Path $inp 'draft-display.tsv'))}
  }else{$a+=@('--trusted-fixture',(Join-Path $ev ($ax+'.ax')))}
  if($n -eq 'korean'){$a+=@('--translation',(Join-Path $inp 'display.tsv'))}
  if($n -eq 'catalog'){$a+=@('--translation',(Join-Path $inp 'runtime.tsv'),'--translation-misses',(Join-Path $d 'misses.tsv'))}
  RunHost $name $a
 }
 $zip=Join-Path $ws 'inputs\elonaplus2.335.zip';$before=Hash $zip
 if($before -ne '06b3fd7d523bcc75ff5c3805d9ae6d5a3056e179b815f27698e8923b1ea21391'){throw 'original ZIP hash'}
 [IO.Compression.ZipFile]::ExtractToDirectory($zip,(Join-Path $run 'private-original'),[Text.Encoding]::GetEncoding(932))
 $game=Join-Path $run 'private-original\elonaplus2.335';$treeBefore=Tree $game
 $packed=Join-Path $run 'packed';$null=New-Item -ItemType Directory -Path $packed
 foreach($n in @('scene1.hsp','scene2.hsp')){Copy-Item -LiteralPath (Join-Path $inp $n) -Destination $packed}
 $stages=@(@{name='newgame';script='newgame-jp.script';waits=4700},@{name='reload';script='load_inspect_resave.script';waits=1400},@{name='world';script='japanese_home_to_world.script';waits=3200})
 foreach($variant in @('reference','managed-fallback','managed-korean')){
  $base=Join-Path $ev $variant;$user=Join-Path $base 'user';$null=New-Item -ItemType Directory -Path $user
  $ax=$(if($variant -eq 'reference'){'reference.ax'}else{'managed.ax'})
  foreach($st in $stages){
   $name=$variant+'/'+$st.name;$d=Join-Path $ev $name;$cwd=Join-Path $d 'cwd';$null=New-Item -ItemType Directory -Path $cwd
   $a=@('--game',(Join-Path $inp $ax),'--game-root',$game,'--user-root',$user,'--work-dir',$cwd,'--packed-dir',$packed,'--headless','--deterministic','--max-waits',"$($st.waits)",'--timeout-ms','120000','--script',(Join-Path $inp $st.script),'--report',(Join-Path $d 'report.json'),'--screenshot',(Join-Path $d 'screen.bmp'),'--trace-commands')
   if($variant -eq 'managed-korean'){$a+=@('--translation',(Join-Path $inp 'runtime.tsv'))}
   if($variant -eq 'managed-korean' -and $manifest.draft_keys){$a+=@('--translation-fallback',(Join-Path $inp 'draft-display.tsv'))}
   RunHost $name $a
   $slot=Join-Path $user 'save\sav_Ari'
   if(Test-Path $slot){Copy-Item -LiteralPath $slot -Destination (Join-Path $d 'save_snapshot') -Recurse}
  }
 }
 $summary.zipUnchanged=(Hash $zip) -eq $before;$summary.privateUnchanged=(Tree $game) -eq $treeBefore
}finally{
 $owned=@(Get-CimInstance Win32_Process|Where-Object{$_.ExecutablePath -eq $hostExe -and $_.CommandLine -and $_.CommandLine.Contains($run)})
 foreach($p in $owned){Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue}
 $summary.leftoverProcesses=@(Get-CimInstance Win32_Process|Where-Object{$_.ExecutablePath -eq $hostExe}).Count
 Json (Join-Path $ev 'summary.json') $summary
 [IO.Compression.ZipFile]::CreateFromDirectory($ev,(Join-Path $run 'evidence.zip'))
}
$summary|ConvertTo-Json -Depth 12
